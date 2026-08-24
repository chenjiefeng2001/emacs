;;; evs55-trace.el --- EVS-5.5 real completion/edit trace.  -*-
;;; lexical-binding: t; -*-

;; Contract: bench/enca/evs5/TRACE.md.  MEASUREMENT ONLY: nothing here
;; changes cache behavior.  Completions record events; scripted edits
;; are classified (EditRelation) against the LIVE context of the last
;; completion; every request is classified H0..H4 vs the previous one
;; and cross-checked against what the CURRENT cache really did (engine
;; source label).
;;
;; Harness-construction note: only scripted edits change the revision,
;; so "E empty => same revision" holds by construction and revisions
;; are not tracked explicitly.
;;
;; Output:
;;   REL55|cell|relation|count
;;   CLS55|cell|hclass|count|engine_miss_count
;;   SUM55|cell|requests=|h0=|h0m=|h1=|h2=|h3=|h4=|safe_pct=|eng_hits=
;;   KPV55|cell|eng_ms|total_ms|src          per request

(require 'cl-lib)

(defun evs55--emit (line)
  (message "%s" line)
  (with-temp-buffer
    (insert line "\n")
    (append-to-file (point-min) (point-max)
                    (or (getenv "EVS53_LOG") "/tmp/evs55_out.txt"))))

(defvar evs55--overlay nil)
(defvar evs55--content nil)
(defvar evs55--cell "CELL")
(defvar evs55--last nil)         ; plist :prefix :cursor :cb :ce
(defvar evs55--ctx-live nil)     ; (beg . end)
(defvar evs55--pfx-live nil)     ; (beg . end)
(defvar evs55--edits-since nil)  ; ((REL BEG DEL-LEN INS-TEXT) newest first)
(defvar evs55--undo-stack nil)   ; ((BEG DEL-LEN DEL-TEXT) ...)
(defvar evs55--stats nil)        ; alist (HCLS . (count . eng-miss))
(defvar evs55--rels nil)         ; alist (REL . count)

(defun evs55--idchar-p (c)
  (or (and (>= c ?a) (<= c ?z)) (and (>= c ?A) (<= c ?Z))
      (and (>= c ?0) (<= c ?9)) (= c ?_)))

(defun evs55--ws-p (s)
  (string-match-p "\\`[ \t\n]*\\'" s))

(defun evs55--ctx-span (pos)
  "Maximal identifier-run span around POS (TRACE.md section 1)."
  (save-excursion
    (goto-char pos)
    (let ((beg pos) (end pos))
      (while (and (> beg (point-min))
                  (evs55--idchar-p (char-after (1- beg))))
        (cl-decf beg))
      (while (and (< end (point-max))
                  (evs55--idchar-p (char-after end)))
        (cl-incf end))
      (cons beg end))))

(defun evs55--content-base ()
  (concat (apply #'concat (make-list 120 "printf_value "))
          (apply #'concat (make-list 120 "beta_symbol "))
          "proc();"))
;; Layout: printf_value soup [0,1560), beta_symbol soup [1560,3000),
;; "proc();" at [3000,3007): proc token [3000,3004), "(" 3004, ")" 3005.

(defun evs55--arm (&optional blank-anchor)
  "Fresh engine session against REAL clangd + displayed buffer.
BLANK-ANCHOR non-nil replaces the alpha_field word with spaces so a
cell can type it character by character (the word's trailing space at
offset 1571 is kept)."
  (setq evs55--content (evs55--content-base))
  (when blank-anchor
    (setq evs55--content
          (concat (substring evs55--content 0 1560)
                  (make-string 11 ?\s)
                  (substring evs55--content 1571))))
  (enca-evs-start 2 nil "/usr/bin/clangd")
  (switch-to-buffer (get-buffer-create "*evs55*"))
  (erase-buffer)
  (insert evs55--content)
  (goto-char (point-min))
  (setq evs55--overlay (make-overlay (point) (+ (point) 3)))
  (setq evs55--last nil evs55--edits-since nil evs55--undo-stack nil
        evs55--ctx-live nil evs55--pfx-live nil
        evs55--stats nil evs55--rels nil)
  ;; didOpen at current revision -- WITHOUT this the first completion
  ;; hits an unopened document and clangd errors (nobackend).
  (enca-evs-lsp-sync evs55--content)
  ;; Prime: absorb any remaining startup race; measurement starts
  ;; only once the server answers normally.
  (let ((tries 0) (src 'nobackend))
    (while (and (< tries 5)
                (eq (setq src (nth 2 (enca-evs-complete "printf" 6)))
                    'nobackend))
      (cl-incf tries))
    (evs55--emit (format "PRIME55|%s|tries=%d|src=%s"
                         evs55--cell tries src))))

(defun evs55--popup (cands)
  (with-temp-buffer
    (let ((n (min 8 (length cands))))
      (dotimes (i n)
        (let* ((lab (nth i cands)) (pad (max 0 (- 36 (length lab)))))
          (insert lab (make-string pad ?\s) " [fn]"))
        (when (< i (1- n)) (insert "\n"))))
    (buffer-string)))

(defun evs55--adjust-iv (iv beg del-len ins-len)
  "Translate interval IV through the edit; return (IV . TOUCHED)."
  (let* ((b (car iv)) (e (cdr iv)) (eend (+ beg del-len))
         (d (- ins-len del-len)))
    (cond
     ((>= beg e) (cons (cons b e) nil))          ; at/after: untouched
     ((<= eend b) (cons (cons (+ b d) (+ e d)) nil)) ; before: shift
     (t (cons (cons (min b beg) (max e eend)) t))))) ; overlap

(defun evs55--rel-of (kind beg del-len ins-text)
  "Classify one edit against LIVE intervals (pre-adjustment).
Point insertions (DEL-LEN 0) landing ON an interval edge count as
INSIDE -- typing appends to the token being completed."
  (let* ((ins-len (length ins-text))
         (end (+ beg del-len))
         (del-text (if (> del-len 0)
                       (buffer-substring beg (+ beg del-len)) ""))
         (cb (car evs55--ctx-live)) (ce (cdr evs55--ctx-live))
         (pb (car evs55--pfx-live)) (pe (cdr evs55--pfx-live)))
    (cond
     ((eq kind 'undo) 'UNDO)
     ((eq kind 'redo) 'REDO)
     ((eq kind 'replace) 'DOCUMENT_REPLACE)
     ((and (= del-len 0) (= ins-len 0)) 'CURSOR_ONLY)
     ((if (= del-len 0)
          (and (>= beg pb) (<= beg pe))
        (and (< beg pe) (> end pb)))
      'INSIDE_PREFIX)
     ((if (= del-len 0)
          (and (>= beg cb) (<= beg ce))
        (and (< beg ce) (> end cb)))
      'INSIDE_CONTEXT)
     ((and (evs55--ws-p ins-text) (evs55--ws-p del-text))
      'WHITESPACE)
     ((<= end cb) 'BEFORE_CONTEXT)
     ((>= beg ce) 'AFTER_CONTEXT)
     (t 'UNKNOWN))))

(defun evs55--do-edit (kind beg del-len ins-text)
  "Apply one scripted edit: classify, mutate buffer, keep bindings."
  (unless evs55--ctx-live
    (error "evs55: edit before first request"))
  (let ((rel (evs55--rel-of kind beg del-len ins-text))
        (del-text (if (> del-len 0)
                      (buffer-substring beg (+ beg del-len)) "")))
    (when (and (> del-len 0) (not (memq kind '(undo redo))))
      (push (list beg del-len del-text) evs55--undo-stack))
    (save-excursion
      (delete-region beg (+ beg del-len))
      (goto-char beg)
      (insert ins-text))
    (setq evs55--content (buffer-string))
    (setq evs55--ctx-live
          (car (evs55--adjust-iv evs55--ctx-live beg del-len
                                 (length ins-text))))
    (setq evs55--pfx-live
          (car (evs55--adjust-iv evs55--pfx-live beg del-len
                                 (length ins-text))))
    (enca-evs-bump-revision)
    (enca-evs-lsp-sync evs55--content)
    (push (list rel beg del-len ins-text) evs55--edits-since)
    (let ((k (assq rel evs55--rels)))
      (if k (setcdr k (1+ (cdr k)))
        (push (cons rel 1) evs55--rels)))
    (evs55--emit (format "EDT55|%s|%s|%d|%d|%s" evs55--cell rel
                         beg del-len
                         (substring ins-text 0 (min 12 (length ins-text)))))
    rel))

(defun evs55--undo-one ()
  "Restore the most recent destroyed span as an UNDO-kind op."
  (if-let ((top (car evs55--undo-stack)))
      (progn
        (pop evs55--undo-stack)
        (evs55--do-edit 'undo (nth 0 top) 0 (nth 2 top)))
    (error "evs55: undo stack empty")))

(defun evs55--near-p (last-ctx)
  "Any intervening edit within NEAR=64B of the ORIGINAL ctx edges?"
  (let ((cb (plist-get last-ctx :cb)) (ce (plist-get last-ctx :ce)))
    (cl-some (lambda (e)
               (let* ((b (nth 1 e)) (dl (nth 2 e)) (end (+ b dl)))
                 (or (<= (abs (- b ce)) 64) (<= (abs (- end cb)) 64))))
             evs55--edits-since)))

(defun evs55--classify-request (prefix cursor)
  (if (null evs55--last)
      'H4
    (let* ((E (reverse evs55--edits-since))
           (rels (mapcar #'car E))
           (same-pfx (string= prefix
                              (plist-get evs55--last :prefix)))
           (same-cur (= cursor (plist-get evs55--last :cursor)))
           (undoish (cl-some (lambda (r)
                               (memq r '(UNDO REDO DOCUMENT_REPLACE
                                               UNKNOWN)))
                             rels))
           (overlap (cl-some (lambda (r)
                               (memq r '(INSIDE_PREFIX INSIDE_CONTEXT)))
                             rels))
           (disj (cl-every (lambda (r)
                             (memq r '(BEFORE_CONTEXT AFTER_CONTEXT
                                       WHITESPACE)))
                           rels)))
      (cond
       ((null E)
        (if (and same-pfx same-cur) 'H0 'H0m))
       ((and overlap same-pfx) 'H2)
       (overlap 'H4)
       ((and disj same-pfx) 'H1)
       ((and disj (evs55--near-p evs55--last)) 'H3)
       (t 'H4)))))

(defun evs55--stat (hcls eng-miss)
  (let ((k (assq hcls evs55--stats)))
    (if k (progn (setcar (cdr k) (1+ (car (cdr k))))
                 (setcdr (cdr k) (+ (cdr (cdr k)) (if eng-miss 1 0))))
      (push (cons hcls (cons 1 (if eng-miss 1 0))) evs55--stats))))

(defun evs55--request (prefix cursor)
  "One completion request: classify, complete, popup+redisplay, record."
  (let* ((t0 (float-time))
         (res (enca-evs-complete prefix cursor))
         (t1 (float-time))
         (cands (nth 0 res)) (src (nth 2 res))
         (str (evs55--popup cands))
         (_ (progn
              (move-overlay evs55--overlay (point) (+ (point) 3))
              (overlay-put evs55--overlay 'after-string str)
              (redisplay t)))
         (tr1 (float-time))
         (eng (* (- t1 t0) 1000.0))
         (total (* (- tr1 t0) 1000.0))
         (hcls (evs55--classify-request prefix cursor))
         (span (evs55--ctx-span cursor)))
    (evs55--stat hcls (not (eq src 'hit)))
    (evs55--emit (format "KPV55|%s|%.4f|%.4f|%s"
                         evs55--cell eng total src))
    (setq evs55--last (list :prefix prefix :cursor cursor
                            :cb (car span) :ce (cdr span))
          evs55--ctx-live span
          evs55--pfx-live (cons (- cursor (length prefix)) cursor)
          evs55--edits-since nil)
    hcls))

(defun evs55--summarize ()
  (let* ((reqs (apply #'+ (mapcar (lambda (s) (car (cdr s)))
                                  evs55--stats)))
         (g (lambda (c) (or (cdr (assq c evs55--stats)) '(0 . 0))))
         (h0 (funcall g 'H0)) (h0m (funcall g 'H0m))
         (h1 (funcall g 'H1)) (h2 (funcall g 'H2))
         (h3 (funcall g 'H3)) (h4 (funcall g 'H4))
         (hits (- reqs (apply #'+ (mapcar (lambda (s) (cdr (cdr s)))
                                          evs55--stats)))))
    (dolist (s evs55--stats)
      (evs55--emit (format "CLS55|%s|%s|%d|%d" evs55--cell
                           (car s) (car (cdr s)) (cdr (cdr s)))))
    (dolist (r (nreverse evs55--rels))
      (evs55--emit (format "REL55|%s|%s|%d" evs55--cell
                           (car r) (cdr r))))
    (evs55--emit
     (format "SUM55|%s|requests=%d|h0=%d|h0m=%d|h1=%d|h2=%d|h3=%d|h4=%d|safe_pct=%.1f|eng_hits=%d"
             evs55--cell reqs (car h0) (car h0m) (car h1) (car h2)
             (car h3) (car h4)
             (/ (* 100.0 (car h1)) (max 1 reqs)) hits))))

;; ---------------- cells ----------------

;; All constants below are BUFFER POSITIONS (1-based), i.e. string
;; offset + 1: alpha_field lives at string [1560,1571), "proc(" at
;; string [3000,3005).
(defconst evs55--alpha-beg 1561)               ; buffer pos of 'a'
(defconst evs55--alpha-word "alpha_field")     ; 11 chars
(defconst evs55--proc-beg 3001)                ; "proc"
(defconst evs55--paren-open 3005)              ; "("

(defun evs55--cell-idgrow ()
  "A: identifier typed character by character at one anchor.
An empty-prefix seed request anchors the context so the very first
keystroke is classifiable (INSIDE_PREFIX at the token edge)."
  (setq evs55--cell "IDGROW")
  (evs55--arm 'blank-anchor)
  (let ((w evs55--alpha-word) (i 1))
    (evs55--request "" evs55--alpha-beg)          ; seed anchor
    (while (<= i (length w))
      ;; typing step: insert next char at the growing token end
      (evs55--do-edit 'edit (+ evs55--alpha-beg (1- i)) 0
                      (substring w (1- i) i))
      (evs55--request (substring w 0 i)
                      (+ evs55--alpha-beg i))
      (setq i (1+ i)))
    ;; exact retry after typing finished
    (evs55--request w (+ evs55--alpha-beg (length w))))
  (evs55--summarize)
  (enca-evs-stop))

(defun evs55--cell-callargs ()
  "B: argument-list growth between repeated identical completions."
  (setq evs55--cell "CALLARGS")
  (evs55--arm)
  (evs55--request "proc" (+ evs55--proc-beg 4)) ; cursor after "proc"
  (let ((argpos (+ evs55--paren-open 1)) (args '("a" ", b" ", c" ", d")))
    (dolist (a args)
      (evs55--do-edit 'edit argpos 0 a)
      (setq argpos (+ argpos (length a)))
      (evs55--request "proc" (+ evs55--proc-beg 4))))
  (evs55--summarize)
  (enca-evs-stop))

(defun evs55--cell-cursor ()
  "C: completions separated by pure point movement (no edits)."
  (setq evs55--cell "CURSOR")
  (evs55--arm)
  (evs55--request "print" 101)
  (goto-char 2401)
  (evs55--request "beta_symbol" 2401)
  (goto-char 101)
  (evs55--request "print" 101)          ; repeat: expect H0 + engine HIT
  (goto-char 104)
  (evs55--request "print" 104)          ; shifted key: H0m, engine MISS
  (goto-char 101)
  (evs55--request "print" 101)          ; back: H0m (key changed again)
  (evs55--summarize)
  (enca-evs-stop))

(defun evs55--cell-faredit ()
  "D: unrelated far-away code edits between identical completions."
  (setq evs55--cell "FAREDIT")
  (evs55--arm)
  (let* ((anchor (+ evs55--alpha-beg 5))       ; after "alpha"
         (far 2991)                            ; tail soup region
         (cur anchor))
    (evs55--request "alpha" cur)               ; establish context
    (dotimes (i 10)
      (let ((snip (format "int uniq_%03d; " (* i 7))))
        (evs55--do-edit 'edit far 0 snip)
        (evs55--request "alpha" cur))))
  (evs55--summarize)
  (enca-evs-stop))

(defun evs55--cell-session ()
  "E: weighted mixed session (TRACE.md section 4)."
  (setq evs55--cell "SESSION")
  (evs55--arm 'blank-anchor)
  (let ((w evs55--alpha-word) (typed 0) (round 0))
    ;; seed anchor context with an empty-prefix request
    (evs55--request "" evs55--alpha-beg)
    (while (< round 25)
      (pcase (% round 5)
        ;; 20%: far unrelated edit + identical re-request (H1)
        (0 (let ((snip (format "double aux_%02d; " round)))
             (evs55--do-edit 'edit 2991 0 snip)
             (evs55--request
              (substring w 0 (max typed 1))
              (+ evs55--alpha-beg (max typed 1)))))
        ;; 20%: growth step (INSIDE_PREFIX -> H4 new request)
        (1 (when (< typed (length w))
             (evs55--do-edit 'edit (+ evs55--alpha-beg typed) 0
                             (substring w typed (1+ typed)))
             (setq typed (1+ typed))
             (evs55--request (substring w 0 typed)
                             (+ evs55--alpha-beg typed))))
        ;; 20%: move elsewhere + request there (H0m / H4-empty)
        (2 (goto-char 2401)
           (evs55--request "beta_sym" 2401)
           (goto-char (+ evs55--alpha-beg typed)))
        ;; 20%: exact retry of the current token state (H0 when the
        ;; previous request was the same key)
        (3 (when (> typed 0)
             (evs55--request (substring w 0 typed)
                             (+ evs55--alpha-beg typed))))
        ;; 20%: undo last far edit, re-request (conservative H4).
        ;; If nothing was deleted yet, synthesize a small deletion in
        ;; the far tail first so the undo has material.
        (4 (unless evs55--undo-stack
             (evs55--do-edit 'edit 2981 6 ""))
           (evs55--undo-one)
           (evs55--request (substring w 0 (max typed 1))
                           (+ evs55--alpha-beg (max typed 1)))))
      (setq round (1+ round))))
  (evs55--summarize)
  (enca-evs-stop))

(message "=== EVS-55 BEGIN ===")
(condition-case err
    (progn
      (evs55--cell-idgrow)
      (evs55--cell-callargs)
      (evs55--cell-cursor)
      (evs55--cell-faredit)
      (evs55--cell-session)
      (message "=== EVS-55 OK ==="))
  (error (message "EVS55 ERROR: %S" err)))
(or (getenv "EVS53_LOG") "/tmp/evs55_out.txt")
(kill-emacs 0)
