;;; evs531-ui-typing.el --- EVS-5.3.1 C13/C8c full version.  -*- lexical-binding: t; -*-

;; Real-typing classes (CACHE.md section 11) through the REAL popup +
;; forced-redisplay path in a live tty Emacs.
;;
;; Per op the FULL span is timed with one clock:
;;   keypress -> enca-evs-complete -> popup overlay install
;;            -> redisplay'       => VISIBLE
;;
;; Output:
;;   KPV31|cell|eng_ms|ui_ms|inst_ms|red_ms|total_ms|src   per op
;;   SUM31|cell|ops=|exact=|extend=|miss=|avoided=%|eng_p50=|
;;         vis_p50=|vis_p95=|vis_p99=|vis_p999=|vis_max=|
;;         inst_p50=|red_p50=                               per cell

(require 'cl-lib)

(defun evs531--emit (line)
  (message "%s" line)
  (with-temp-buffer
    (insert line "\n")
    (append-to-file (point-min) (point-max)
                    (or (getenv "EVS53_LOG") "/tmp/evs531_out.txt"))))

(defun evs531--pct (l p)
  "Nearest-rank P percentile of list L (P in 0..100)."
  (let ((s (sort (copy-sequence l) #'<))
        (n (length l)))
    (nth (max 0 (min (1- n) (1- (ceiling (* (/ p 100.0) n))))) s)))

(defun evs531--popup-rows (cands rows width)
  (with-temp-buffer
    (let ((n (min rows (length cands))))
      (dotimes (i n)
        (let* ((lab (nth i cands))
               (pad (max 0 (- width (length lab)))))
          (insert lab (make-string pad ?\s) " [fn]"))
        (when (< i (1- n)) (insert "\n"))))
    (buffer-string)))

(defvar evs531--overlay nil)

(defun evs531--op (cell prefix cursor rows)
  "One full user-path op; returns (total eng inst red src)."
  (let* ((t0 (float-time))
         (res (enca-evs-complete prefix cursor))
         (t1 (float-time))
         (cands (nth 0 res))
         (eng (nth 1 res))
         (src (nth 2 res))
         (ti0 (float-time))
         (str (evs531--popup-rows cands rows 36))
         (ti1 (float-time))
         (tr0 (float-time)))
    (move-overlay evs531--overlay (point) (+ (point) 3))
    (overlay-put evs531--overlay 'after-string str)
    (redisplay t)
    (let* ((tr1 (float-time))
           (inst (* (- ti1 ti0) 1000.0))
           (red  (* (- tr1 tr0) 1000.0))
           (total (* (- tr1 t0) 1000.0))
           (eng-wall (* (- t1 t0) 1000.0)))
      (evs531--emit
       (format "KPV31|%s|%.4f|%.4f|%.4f|%.4f|%.4f|%s"
               cell eng-wall (+ inst red) inst red total src))
      (list total eng-wall inst red src))))

(defun evs531--sum (name ops exact ext miss eng-lats tot-lats inst-lats red-lats)
  (let ((n (max 1 (+ exact ext miss)))
        (tt (sort (copy-sequence tot-lats) #'<)))
    (evs531--emit
     (format (concat "SUM31|%s|ops=%d|exact=%d|extend=%d|miss=%d|"
                     "avoided=%.1f%%|eng_p50=%.4f|vis_p50=%.4f|"
                     "vis_p95=%.4f|vis_p99=%.4f|vis_p99.9=%.4f|"
                     "vis_max=%.4f|inst_p50=%.4f|red_p50=%.4f")
             name ops exact ext miss
             (/ (* 100.0 (+ exact ext)) n)
             (evs531--pct eng-lats 50)
             (evs531--pct tt 50) (evs531--pct tt 95)
             (evs531--pct tt 99) (evs531--pct tt 99.9)
             (car (last tt))
             (evs531--pct inst-lats 50)
             (evs531--pct red-lats 50)))))

(defun evs531--arm-setup (fillchar)
  "Fresh engine session + displayed buffer + overlay."
  (enca-evs-start 1 nil 'loopback)
  (switch-to-buffer (get-buffer-create "*evs531*"))
  (erase-buffer)
  (insert (make-string 8000 fillchar))
  (goto-char (point-max))
  (setq evs531--overlay (make-overlay (point) (+ (point) 3))))

(defun evs531--classify (src n)
  (pcase src
    ('hit (if (>= n 4) 'extend 'exact))
    (_ 'miss)))

(defvar evs531--vocab
  ["print" "printf" "println" "private" "process" "progress"
   "project" "protect" "profile" "program" "prompt" "propagate"])

(defun evs531--retry-cell ()
  ;; RETRY: prime once, then identical requests => hit-exact class.
  (evs531--arm-setup ?a)
  (enca-evs-complete "print" (+ (point) 5))     ; prime (unmeasured)
  (let ((tot nil) (eng nil) (ins nil) (red nil) (exact 0) (ext 0) (miss 0) (n 30))
    (dotimes (_ n)
      (let* ((r (evs531--op "RETRY" "print" (+ (point) 5) 10))
             (cls (evs531--classify (nth 4 r) 1)))
        (push (nth 0 r) tot) (push (nth 1 r) eng)
        (push (nth 2 r) ins) (push (nth 3 r) red)
        (cl-case cls (exact (setq exact (1+ exact)))
          (extend (setq ext (1+ ext)))
          (t (setq miss (1+ miss))))))
    (evs531--sum "RETRY" n exact ext miss eng tot ins red))
  (enca-evs-stop))

(defun evs531--growth-cell ()
  ;; GROWTH: same workload as section 10 identifier-growth cell.
  (evs531--arm-setup ?b)
  (let ((tot nil) (eng nil) (ins nil) (red nil) (exact 0) (ext 0) (miss 0) (i 0))
    (dotimes (_ 32)
      (let* ((w (aref evs531--vocab (/ i 4)))
             (nn (1+ (% i (length w))))
             (prefix (substring w 0 nn))
             (r (evs531--op "GROWTH" prefix 4000 10))
             (cls (evs531--classify (nth 4 r) nn)))
        (setq i (1+ i))
        (push (nth 0 r) tot) (push (nth 1 r) eng)
        (push (nth 2 r) ins) (push (nth 3 r) red)
        (cl-case cls (exact (setq exact (1+ exact)))
          (extend (setq ext (1+ ext)))
          (t (setq miss (1+ miss))))))
    (evs531--sum "GROWTH" i exact ext miss eng tot ins red))
  (enca-evs-stop))

(defun evs531--novel-cell ()
  ;; NOVEL: fresh prefixes, constant revision => honest misses.
  (evs531--arm-setup ?c)
  (let ((tot nil) (eng nil) (ins nil) (red nil) (exact 0) (ext 0) (miss 0) (n 15))
    (dotimes (i n)
      (let* ((prefix (format "sym%03d" i))
             (r (evs531--op "NOVEL" prefix (+ (point) (length prefix)) 10)))
        (push (nth 0 r) tot) (push (nth 1 r) eng)
        (push (nth 2 r) ins) (push (nth 3 r) red)
        (if (eq (nth 4 r) 'hit) (setq exact (1+ exact)) (setq miss (1+ miss)))))
    (evs531--sum "NOVEL" n exact ext miss eng tot ins red))
  (enca-evs-stop))

(defun evs531--editmix-cell ()
  ;; EDITMIX: completion then revision bump each iteration =>
  ;; miss by design (conservative invalidation).
  (evs531--arm-setup ?d)
  (let ((tot nil) (eng nil) (ins nil) (red nil) (exact 0) (ext 0) (miss 0) (n 12))
    (dotimes (i n)
      (let* ((prefix (format "edt%03d" (% i 300)))
             (r (evs531--op "EDITMIX" prefix (+ 100 (* i 50)) 10)))
        (push (nth 0 r) tot) (push (nth 1 r) eng)
        (push (nth 2 r) ins) (push (nth 3 r) red)
        (if (eq (nth 4 r) 'miss) (setq miss (1+ miss)) (setq exact (1+ exact))))
      (enca-evs-bump-revision))
    (evs531--sum "EDITMIX" n exact ext miss eng tot ins red))
  (enca-evs-stop))

(message "=== EVS-531 BEGIN ===")
(random 42)
(condition-case err
    (progn
      (evs531--retry-cell)
      (evs531--growth-cell)
      (evs531--novel-cell)
      (evs531--editmix-cell)
      (message "=== EVS-531 OK ==="))
  (error (message "EVS531 ERROR: %S" err)))
(let ((file (or (getenv "EVS53_LOG") "/tmp/evs531_out.txt")))
  file)
(kill-emacs 0)
