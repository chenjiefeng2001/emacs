;;; evs54-real-lsp.el --- EVS-5.4 real LSP through the elisp user
;;; path.  -*- lexical-binding: t; -*-

;; Contract: bench/enca/evs5/REAL_LSP.md.  Same shapes/clock semantics
;; as evs531-ui-typing.el, but the MISS arm talks to a REAL clangd
;; spawned at enca-evs-start time (no injected delay anywhere).
;;
;; Output:
;;   KPV54|cell|eng_ms|ui_ms|inst_ms|red_ms|total_ms|src   per op
;;   SUM54|cell|ops=|exact=|extend=|miss=|avoided=%|eng_p50=|
;;         vis_p50=|vis_p95=|vis_p99=|vis_p99.9=|vis_max=|
;;         inst_p50=|red_p50=                               per cell

(require 'cl-lib)

(defun evs54--emit (line)
  (message "%s" line)
  (with-temp-buffer
    (insert line "\n")
    (append-to-file (point-min) (point-max)
                    (or (getenv "EVS53_LOG") "/tmp/evs54_out.txt"))))

(defun evs54--pct (l p)
  "Nearest-rank P percentile of list L (P in 0..100)."
  (let ((s (sort (copy-sequence l) #'<))
        (n (length l)))
    (nth (max 0 (min (1- n) (1- (ceiling (* (/ p 100.0) n))))) s)))

(defun evs54--popup-rows (cands rows width)
  (with-temp-buffer
    (let ((n (min rows (length cands))))
      (dotimes (i n)
        (let* ((lab (nth i cands))
               (pad (max 0 (- width (length lab)))))
          (insert lab (make-string pad ?\s) " [fn]"))
        (when (< i (1- n)) (insert "\n"))))
    (buffer-string)))

(defvar evs54--overlay nil)
(defvar evs54--content nil)

(defun evs54--arm ()
  "Fresh engine session against REAL /usr/bin/clangd + displayed
single-line ASCII buffer + overlay."
  ;; Single-line symbol-soup document: byte == UTF-8 == UTF-16 offsets,
  ;; so line=0 character=<byte offset> position mapping is exact.
  (setq evs54--content
        (concat (apply #'concat (make-list 340 "printf_value "))
                "int process_id;"))
  (enca-evs-start 2 nil "/usr/bin/clangd")
  (switch-to-buffer (get-buffer-create "*evs54*"))
  (erase-buffer)
  (insert evs54--content)
  (goto-char (point-max))
  (setq evs54--overlay (make-overlay (point) (+ (point) 3)))
  ;; Document identity server-side: didOpen at the current revision.
  (enca-evs-lsp-sync evs54--content))

(defun evs54--op (cell prefix cursor rows)
  "One full user-path op; returns (total eng inst red src)."
  (let* ((t0 (float-time))
         (res (enca-evs-complete prefix cursor))
         (t1 (float-time))
         (cands (nth 0 res))
         (eng (nth 1 res))
         (src (nth 2 res))
         (ti0 (float-time))
         (str (evs54--popup-rows cands rows 36))
         (ti1 (float-time))
         (tr0 (float-time)))
    (move-overlay evs54--overlay (point) (+ (point) 3))
    (overlay-put evs54--overlay 'after-string str)
    (redisplay t)
    (let* ((tr1 (float-time))
           (inst (* (- ti1 ti0) 1000.0))
           (red  (* (- tr1 tr0) 1000.0))
           (total (* (- tr1 t0) 1000.0))
           (eng-wall (* (- t1 t0) 1000.0)))
      (evs54--emit
       (format "KPV54|%s|%.4f|%.4f|%.4f|%.4f|%.4f|%s"
               cell eng-wall (+ inst red) inst red total src))
      (list total eng-wall inst red src))))

(defun evs54--sum (name ops exact ext miss eng-lats tot-lats inst-lats red-lats)
  (let ((n (max 1 (+ exact ext miss)))
        (tt (sort (copy-sequence tot-lats) #'<)))
    (evs54--emit
     (format (concat "SUM54|%s|ops=%d|exact=%d|extend=%d|miss=%d|"
                     "avoided=%.1f%%|eng_p50=%.4f|vis_p50=%.4f|"
                     "vis_p95=%.4f|vis_p99=%.4f|vis_p99.9=%.4f|"
                     "vis_max=%.4f|inst_p50=%.4f|red_p50=%.4f")
             name ops exact ext miss
             (/ (* 100.0 (+ exact ext)) n)
             (evs54--pct eng-lats 50)
             (evs54--pct tt 50) (evs54--pct tt 95)
             (evs54--pct tt 99) (evs54--pct tt 99.9)
             (car (last tt))
             (evs54--pct inst-lats 50)
             (evs54--pct red-lats 50)))))

;; Classification identical to evs531-ui-typing.el: a hit on a request
;; whose prefix length is >= 4 is counted as Stage-B extend (the grow
;; path serves longer stored prefixes), shorter ones as exact.
(defun evs54--classify (src n)
  (pcase src
    ('hit (if (>= n 4) 'extend 'exact))
    (_ 'miss)))

(defvar evs54--vocab
  ["print" "printf" "println" "private" "process" "progress"
   "project" "protect" "profile" "program" "prompt" "propagate"])

(defun evs54--cold-cell ()
  ;; COLD: very first completion after start; rides clangd's initial
  ;; parse/index burst.  n=1, reported, never hidden (REAL_LSP.md 3).
  (evs54--arm)
  (let* ((r (evs54--op "COLD" "prin" 200 10))
         (cls (evs54--classify (nth 4 r) 4)))
    (evs54--sum "COLD" 1 (if (eq cls 'exact) 1 0) 0
                (if (eq cls 'miss) 1 0)
                (list (nth 1 r)) (list (nth 0 r))
                (list (nth 2 r)) (list (nth 3 r))))
  (enca-evs-stop))

(defun evs54--retry-cell ()
  ;; RETRY: prime once, then identical requests => hit-exact class.
  (evs54--arm)
  (enca-evs-complete "print" 4017)          ; prime (unmeasured)
  (let ((tot nil) (eng nil) (ins nil) (red nil) (exact 0) (ext 0) (miss 0) (n 30))
    (dotimes (_ n)
      (let* ((r (evs54--op "RETRY" "print" 4017 10))
             (cls (evs54--classify (nth 4 r) 1)))
        (push (nth 0 r) tot) (push (nth 1 r) eng)
        (push (nth 2 r) ins) (push (nth 3 r) red)
        (cl-case cls (exact (setq exact (1+ exact)))
          (extend (setq ext (1+ ext)))
          (t (setq miss (1+ miss))))))
    (evs54--sum "RETRY" n exact ext miss eng tot ins red))
  (enca-evs-stop))

(defun evs54--growth-cell ()
  ;; GROWTH: section 9.3 workload unchanged (CACHE.md cross-check:
  ;; must reproduce F1 exactly => exact=9 extend=2 miss=21).
  (evs54--arm)
  (let ((tot nil) (eng nil) (ins nil) (red nil) (exact 0) (ext 0) (miss 0) (i 0))
    (dotimes (_ 32)
      (let* ((w (aref evs54--vocab (/ i 4)))
             (nn (1+ (% i (length w))))
             (prefix (substring w 0 nn))
             (r (evs54--op "GROWTH" prefix 4000 10))
             (cls (evs54--classify (nth 4 r) nn)))
        (setq i (1+ i))
        (push (nth 0 r) tot) (push (nth 1 r) eng)
        (push (nth 2 r) ins) (push (nth 3 r) red)
        (cl-case cls (exact (setq exact (1+ exact)))
          (extend (setq ext (1+ ext)))
          (t (setq miss (1+ miss))))))
    (evs54--sum "GROWTH" i exact ext miss eng tot ins red))
  (enca-evs-stop))

(defun evs54--novel-cell ()
  ;; NOVEL: fresh prefixes, constant revision => honest misses through
  ;; the real backend round trip (the headline number of this phase).
  (evs54--arm)
  (let ((tot nil) (eng nil) (ins nil) (red nil) (exact 0) (ext 0) (miss 0) (n 15))
    (dotimes (i n)
      (let* ((prefix (format "sym%03d" i))
             (r (evs54--op "NOVEL" prefix 4200 10)))
        (push (nth 0 r) tot) (push (nth 1 r) eng)
        (push (nth 2 r) ins) (push (nth 3 r) red)
        (if (eq (nth 4 r) 'hit) (setq exact (1+ exact)) (setq miss (1+ miss)))))
    (evs54--sum "NOVEL" n exact ext miss eng tot ins red))
  (enca-evs-stop))

(defun evs54--editmix-cell ()
  ;; EDITMIX: revision bump + document sync + completion each
  ;; iteration => miss by design (conservative invalidation); clangd's
  ;; view stays version-bound to the ENCA revision throughout.
  (evs54--arm)
  (let ((tot nil) (eng nil) (ins nil) (red nil) (exact 0) (ext 0) (miss 0) (n 12))
    (dotimes (i n)
      (let* ((prefix (format "edt%03d" (% i 300)))
             (r (progn
                  (enca-evs-bump-revision)
                  (enca-evs-lsp-sync evs54--content)
                  (evs54--op "EDITMIX" prefix (+ 100 (* i 50)) 10))))
        (push (nth 0 r) tot) (push (nth 1 r) eng)
        (push (nth 2 r) ins) (push (nth 3 r) red)
        (if (eq (nth 4 r) 'miss) (setq miss (1+ miss)) (setq exact (1+ exact)))))
    (evs54--sum "EDITMIX" n exact ext miss eng tot ins red))
  (enca-evs-stop))

(message "=== EVS-54 BEGIN ===")
(condition-case err
    (progn
      (evs54--cold-cell)
      (evs54--retry-cell)
      (evs54--growth-cell)
      (evs54--novel-cell)
      (evs54--editmix-cell)
      (message "=== EVS-54 OK ==="))
  (error (message "EVS54 ERROR: %S" err)))
(let ((file (or (getenv "EVS53_LOG") "/tmp/evs54_out.txt")))
  file)
(kill-emacs 0)
