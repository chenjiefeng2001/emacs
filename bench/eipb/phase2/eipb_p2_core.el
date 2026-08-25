;;; eipb_p2_core.el --- EIPB Phase 2 tail-latency attribution -*- lexical-binding: t; -*-

;; Contract: bench/eipb/EIPB.md section 7.  Three frozen questions:
;;   Q1 font-lock chain   (FL section)
;;   Q2 GC in user path   (GCPATH section)
;;   Q3 window/buffer     (WB section)
;;
;; Output lines to EIPB_LOG:
;;   EIPB2|<build>|<cell>|<metric>|<value>
;;   EIPB2|<build>|<cell>|msg|<text>          (human notes / errors)
;;
;; Sections are selected with EIPB_P2_SECTION = FL | GCPATH | WB | ALL
;; (default ALL) so the runner can give each its own timeout budget.
;;
;; Fault isolation: every cell / arm / subsection is wrapped in its own
;; condition-case; an error is noted into the log and the run moves on.
;; A single bad cell must never again kill Q2/Q3 (session-2 lesson).

(require 'cl-lib)

(defconst eipb2--build (or (getenv "EIPB_BUILD") "UNKNOWN"))

(defun eipb2--log ()
  (or (getenv "EIPB_LOG") "/tmp/eipb_t2_out.txt"))

(defun eipb2--append-line (line)
  (with-temp-buffer
    (insert line)
    (append-to-file (point-min) (point-max) (eipb2--log))))

(defun eipb2--emit (cell metric value)
  (eipb2--append-line
   (format "EIPB2|%s|%s|%s|%.4f\n" eipb2--build cell metric value)))

(defun eipb2--note (cell text)
  (eipb2--append-line
   (format "EIPB2|%s|%s|msg|%s\n" eipb2--build cell text)))

(defun eipb2--pct (l p)
  (let ((s (sort (copy-sequence l) #'<)) (n (length l)))
    (nth (max 0 (min (1- n) (1- (ceiling (* (/ p 100.0) n))))) s)))

(defun eipb2--max (cell l)
  (eipb2--emit cell "max" (car (last (sort (copy-sequence l) #'<)))))

(defun eipb2--nums (l)
  (cl-loop for x in l when (numberp x) collect x))

(defun eipb2--dist (cell l)
  (setq l (eipb2--nums l))
  (when l
    (eipb2--emit cell "n" (float (length l)))
    (dolist (p '(50 95 99))
      (eipb2--emit cell (format "p%d" p) (eipb2--pct l p)))
    (eipb2--max cell l)))

(defun eipb2--stalls (cell l)
  (setq l (eipb2--nums l))
  (eipb2--emit cell "stall_gt_10ms"
               (float (cl-count-if (lambda (x) (> x 10)) l)))
  (eipb2--emit cell "stall_gt_50ms"
               (float (cl-count-if (lambda (x) (> x 50)) l)))
  (eipb2--emit cell "stall_gt_100ms"
               (float (cl-count-if (lambda (x) (> x 100)) l))))

(defun eipb2--timed-reps (n fn)
  "Run FN N times; return list of per-call latencies in ms."
  (let ((lats nil))
    (dotimes (_ n)
      (let ((t0 (float-time)))
        (funcall fn)
        (push (* (- (float-time) t0) 1000.0) lats)))
    (nreverse lats)))

(defun eipb2--gc-in-op-p (before after)
  (and (numberp before) (numberp after) (> after before)))

;; ---------------- content ----------------

(defun eipb2--unit (lang i)
  (pcase lang
    ("c"      (format "/* c%d */\nstatic int fn_%d(int a, int b)\n{\n    return a + b + %d;\n}\n\n" i i i))
    ("elisp"  (format "(defun fn-%d (a b)\n  \"Doc %d.\"\n  (+ a b %d))\n\n" i i i))
    ("python" (format "def fn_%d(a, b):\n    \"\"\"Doc %d.\"\"\"\n    return a + b + %d\n\n\n" i i i))
    ("org"    (format "* Heading %d\nBody with *bold* and [[https://ex][link]] text.\n** Sub %d\n- item one\n\n" i i))
    (_        (format "printf_value_%d " i))))

(defun eipb2--make-buffer (name lang bytes mode-sym)
  (switch-to-buffer (get-buffer-create name))
  (setq buffer-undo-list t)
  (erase-buffer)
  (let ((i 0))
    (while (< (- (point-max) (point-min)) bytes)
      (insert (eipb2--unit lang i))
      (setq i (1+ i))))
  (funcall mode-sym)
  (font-lock-mode 1)
  (current-buffer))

;; ---------------- Q1: FONT-LOCK CHAIN ----------------

(defvar eipb2--modes
  '(("c"      . c-mode)
    ("elisp"  . emacs-lisp-mode)
    ("python" . python-mode)
    ("org"    . org-mode)
    ("plain"  . fundamental-mode)))

(defvar eipb2--fl-specs
  '(("c" 100000 40 30) ("c" 1040000 40 30)
    ("elisp" 100000 40 30) ("elisp" 1040000 40 30)
    ("python" 100000 40 30) ("python" 1040000 40 30)
    ("org" 100000 40 30) ("org" 1040000 40 30)
    ("plain" 100000 40 30) ("plain" 1040000 40 30)
    ;; runtime-cap cells LAST: if the wall clock dies here, all
    ;; smaller cells have already landed (documented scope cut).
    ("c" 10400000 10 5) ("plain" 10400000 10 5)))

(defun eipb2--fl-tag (bytes)
  (cond ((>= bytes 10000000) "10MB")
        ((>= bytes 1000000)  "1MB")
        (t                   "100KB")))

(defun eipb2--fl-cell (lang bytes reps-nat reps-dec)
  (let* ((tag (format "fl/%s/%s" lang (eipb2--fl-tag bytes)))
         (buf (eipb2--make-buffer
               (format "*fl-%s*" lang) lang bytes
               ;; assoc, not assq -- the keys are strings.
               (cdr (assoc lang eipb2--modes)))))
    ;; whole-buffer fontification baseline (cold, once)
    (let ((t0 (float-time)))
      (font-lock-ensure)
      (eipb2--emit tag "full_fontify_ms" (* (- (float-time) t0) 1000.0)))
    ;; park point mid-buffer, on screen, so jit cannot defer to stealth
    (goto-char (/ (+ (point-min) (point-max)) 2))
    (redisplay t)
    ;; Control-arm guard: in fundamental buffers font-lock-mode does
    ;; NOT register jit-lock-functions, and upstream jit-lock--run-
    ;; functions then crashes on (min nil beg) (reproduced in batch,
    ;; identical in vanilla).  Fontification is a semantic no-op
    ;; there, so we skip the jit calls, leave the segments at ~0 ms
    ;; and say so once.
    (unless jit-lock-functions
      (eipb2--note tag "no_jit_lock_functions_fontify_skipped"))
    ;; warm one full natural cycle (first-op jit is cold); a control-
    ;; arm error here is noted but must not discard the cell
    (condition-case err
        (progn
          (insert "x")
          (when jit-lock-functions
            (jit-lock-fontify-now (- (point) 200) (+ (point) 200)))
          (redisplay t)
          (delete-char -1)
          (redisplay t))
      (error (eipb2--note tag (format "warm_error %S" err))))
    ;; ---- NATURAL arm: single clock over insert+redisplay ----
    (let ((lats nil) (g0 gcs-done) (gcdur 0))
      (dotimes (_ reps-nat)
        (let* ((ta (float-time))
               (gr gcs-done))
          (insert "x")
          (redisplay t)
          (when (eipb2--gc-in-op-p gr gcs-done) (cl-incf gcdur))
          (push (* (- (float-time) ta) 1000.0) lats)))
      (eipb2--dist (concat tag "/natural") lats)
      (eipb2--stalls (concat tag "/natural") lats)
      (eipb2--emit (concat tag "/natural") "ops_with_gc" (float gcdur))
      (eipb2--emit (concat tag "/natural") "gcs_delta"
                   (float (max 0 (- (or gcs-done 0) (or g0 0))))))
    ;; ---- DECOMP arm: buffer / fontify / redisplay segments ----
    (let ((lb nil) (lf nil) (lr nil))
      (dotimes (_ reps-dec)
        (let* ((ta (float-time)))
          (insert "x")
          (let ((tb (float-time)))
            (when jit-lock-functions
              (jit-lock-fontify-now (- (point) 400) (+ (point) 400)))
            (let ((tc (float-time)))
              (redisplay t)
              (push (* (- (float-time) tc) 1000.0) lr)
              (push (* (- tc tb) 1000.0) lf)
              (push (* (- tb ta) 1000.0) lb)))))
      (eipb2--dist (concat tag "/decomp-buf") lb)
      (eipb2--dist (concat tag "/decomp-font") lf)
      (eipb2--dist (concat tag "/decomp-red") lr))
    (kill-buffer buf)))

(defun eipb2--fl-section ()
  (dolist (spec eipb2--fl-specs)
    (condition-case err
        (eipb2--fl-cell (nth 0 spec) (nth 1 spec)
                        (nth 2 spec) (nth 3 spec))
      (error (eipb2--emit (format "fl/%s/%s" (nth 0 spec)
                                  (eipb2--fl-tag (nth 1 spec)))
                          "cell_error" 0.0)
             (eipb2--note (format "fl/%s/%s" (nth 0 spec)
                                  (eipb2--fl-tag (nth 1 spec)))
                          (format "cell_error %S" err))))))

;; ---------------- Q2: GC IN USER PATH ----------------

(defun eipb2--gcpath-arm (label force-every threshold)
  (when threshold (setq gc-cons-threshold threshold))
  (garbage-collect)
  (let ((lats nil) (g0 gcs-done) (gcdur 0))
    (dotimes (i 1000)
      (let* ((ta (float-time))
             (gr gcs-done))
        (insert "x")
        (redisplay t)
        (when (and force-every (= (% (1+ i) force-every) 0))
          (garbage-collect))
        (when (eipb2--gc-in-op-p gr gcs-done) (cl-incf gcdur))
        (push (* (- (float-time) ta) 1000.0) lats)))
    (eipb2--dist (concat "gcp/" label) lats)
    (eipb2--stalls (concat "gcp/" label) lats)
    (eipb2--emit (concat "gcp/" label) "ops_with_gc" (float gcdur))
    (eipb2--emit (concat "gcp/" label) "gc_count_delta"
                 (float (max 0 (- (or gcs-done 0) (or g0 0)))))))

(defun eipb2--gcpath-section ()
  (switch-to-buffer (get-buffer-create "*gcp*"))
  (setq buffer-undo-list t)
  (erase-buffer)
  (dotimes (_ 80000) (insert "printf_value "))
  (goto-char (point-max))
  (fundamental-mode)
  (redisplay t)
  (let ((saved-threshold gc-cons-threshold))
    ;; NOTE: plain quoted data -- numbers only, no unevaluated forms
    ;; (session-3 lesson: (* 64 1024 1024) inside a quote stays a list).
    (dolist (arm '(("nogc"    nil 67108864)
                   ("natural" nil 20000)
                   ("forced"  25  nil)))
      (condition-case err
          (eipb2--gcpath-arm (nth 0 arm) (nth 1 arm) (nth 2 arm))
        (error (eipb2--emit (concat "gcp/" (nth 0 arm)) "arm_error" 0.0)
               (eipb2--note (concat "gcp/" (nth 0 arm))
                            (format "arm_error %S" err)))))
    (setq gc-cons-threshold saved-threshold))
  (kill-buffer "*gcp*"))

;; ---------------- Q3: WINDOW/BUFFER ----------------

(defun eipb2--set-windows (n)
  (delete-other-windows)
  (while (< (length (window-list)) n)
    (select-window (get-largest-window))
    (split-window-below)))

(defun eipb2--wb-popup ()
  "One popup+visible cycle; returns latency in ms.
NOTE: must RETURN the value -- pushing through a parameter only
mutates the local binding (lexical binding), the caller's list
would stay empty and the dist would be silently skipped."
  (let ((t0 (float-time))
         (ov (make-overlay (point) (+ (point) 3)))
         (str "candidate-01 [fn]\ncandidate-02 [fn]"))
    (move-overlay ov (point) (+ (point) 3))
    (overlay-put ov 'after-string str)
    (redisplay t)
    (delete-overlay ov)
    (* (- (float-time) t0) 1000.0)))

(defun eipb2--windows-scaling ()
  (eipb2--make-buffer "*wb-main*" "plain" 1040000 'fundamental-mode)
  (goto-char (/ (point-max) 2))
  (dolist (n '(1 2 4 8))
    (eipb2--set-windows n)
    (redisplay t)
    (let ((pre (format "win%d" n)))
      (eipb2--dist (concat pre "/noop")
                   (eipb2--timed-reps 100 (lambda () (redisplay t))))
      (eipb2--dist (concat pre "/editvis")
                   (eipb2--timed-reps
                    50 (lambda () (insert "x") (redisplay t))))
      (let ((pl nil))
        (dotimes (_ 50) (push (eipb2--wb-popup) pl))
        (eipb2--dist (concat pre "/popup") pl))
      (let ((sl nil))
        (dotimes (i 30)
          (let* ((t0 (float-time)))
            (if (= 0 (% i 2)) (scroll-up-line) (scroll-down-line))
            (redisplay t)
            (push (* (- (float-time) t0) 1000.0) sl)))
        (eipb2--dist (concat pre "/scroll") sl))))
  (delete-other-windows))

(defun eipb2--make-wb-buffers (k)
  (let ((bufs nil))
    (dotimes (i k)
      (let ((b (get-buffer-create (format "*wb-%03d*" i))))
        (with-current-buffer b
          (setq buffer-undo-list t)
          (erase-buffer)
          (dotimes (_ 8000) (insert "beta_symbol "))
          (goto-char (/ (point-max) 2)))
        (push b bufs)))
    (nreverse bufs)))

(defun eipb2--buffers-scaling ()
  (delete-other-windows)
  (dolist (k '(10 100))
    (let ((bufs (eipb2--make-wb-buffers k))
          (lats nil))
      (switch-to-buffer (nth 0 bufs))
      (goto-char (/ (point-max) 2))
      (redisplay t)
      (dotimes (i (* 2 k))
        (let* ((t0 (float-time)))
          (switch-to-buffer (nth (% (1+ i) k) bufs))
          (redisplay t)
          (push (* (- (float-time) t0) 1000.0) lats)))
      (eipb2--dist (format "buf%d/switch" k) lats)
      (eipb2--dist (format "buf%d/editvis" k)
                   (eipb2--timed-reps
                    30 (lambda () (insert "x") (redisplay t))))
      (eipb2--emit (format "buf%d" k) "memlimit_kb"
                   (float (memory-limit)))
      (unless (= k 100)               ; keep the 100 set for spotlight
        (dolist (b bufs) (kill-buffer b))))))

(defun eipb2--spotlight (bufs)
  (eipb2--set-windows 8)
  (switch-to-buffer (nth 0 bufs))
  (goto-char (/ (point-max) 2))
  (redisplay t)
  (eipb2--dist "spot8x100/editvis"
               (eipb2--timed-reps
                30 (lambda () (insert "x") (redisplay t))))
  (let ((pl nil))
    (dotimes (_ 30) (push (eipb2--wb-popup) pl))
    (eipb2--dist "spot8x100/popup" pl))
  (let ((sw nil))
    (dotimes (i 30)
      (let* ((t0 (float-time)))
        (switch-to-buffer (nth (% (1+ i) (length bufs)) bufs))
        (redisplay t)
        (push (* (- (float-time) t0) 1000.0) sw)))
    (eipb2--dist "spot8x100/switch" sw))
  (delete-other-windows))

(defun eipb2--wb-section ()
  (eipb2--emit "wb" "memlimit_kb_start" (float (memory-limit)))
  (eipb2--windows-scaling)
  (eipb2--buffers-scaling)
  (let ((bufs nil))
    (dotimes (i 100)
      (push (get-buffer-create (format "*wb-%03d*" i)) bufs))
    (setq bufs (nreverse bufs))
    (eipb2--spotlight bufs)
    (eipb2--emit "wb" "memlimit_kb_end" (float (memory-limit)))
    (dolist (b bufs) (kill-buffer b))))

;; ---------------- main ----------------

(defun eipb2--run-section (name)
  (condition-case err
      (pcase name
        ("FL"     (require 'python) (require 'org)
                  (eipb2--fl-section))
        ("GCPATH" (eipb2--gcpath-section))
        ("WB"     (eipb2--wb-section)))
    (error (eipb2--emit "FATAL" "error" 0.0)
           (eipb2--note "FATAL" (format "%s ERROR: %S" name err)))))

(message "=== EIPB P2 BEGIN (%s) ===" eipb2--build)
;; Q3 needs 8 stacked windows: a default 24-row pty cannot host them
;; (window-min-height 4).  Resize first; ignore errors on exotic ttys.
(ignore-errors (set-frame-height (selected-frame) 50))
(ignore-errors (set-frame-width  (selected-frame) 170))
(let ((want (or (getenv "EIPB_P2_SECTION") "ALL")))
  (if (member want '("FL" "GCPATH" "WB"))
      (eipb2--run-section want)
    (dolist (s '("FL" "GCPATH" "WB"))
      (eipb2--run-section s))))
(kill-emacs 0)
