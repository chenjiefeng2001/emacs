;;; evs53-typing.el --- EVS-5.3 real typing workload closure.  -*- lexical-binding: t; -*-

;; Arms:
;;   identifier-growth : words typed char-by-char at one anchor
;;                       (exercises Stage-B extend hits)
;;   retry-backend90   : identical request repeated; backend = fake
;;                       server style miss cost is simulated by
;;                       EVS_BACKEND_DELAY_MS on the loopback session
;;   edit-mixed        : completions interleaved with revision bumps
;;
;; Output:
;;   TYPING|cell|ops=|exact=|extend=|miss=|avoided=%|p50|p95|max

(defun evs53--emit (line)
  (message "%s" line)
  (with-temp-buffer
    (insert line "\n")
    (append-to-file (point-min) (point-max)
                    (or (getenv "EVS53_LOG") "/tmp/evs53_out.txt"))))

(defun evs53--pcts (l)
  (let ((s (sort (copy-sequence l) #'<)))
    (list (nth (max 0 (min (1- (length s)) (floor (* 0.50 (length s))))) s)
          (nth (max 0 (min (1- (length s)) (floor (* 0.95 (length s))))) s)
          (car (last s)))))

(defvar evs53--vocab
  ["print" "printf" "println" "private" "process" "progress"
   "project" "protect" "profile" "program" "prompt" "propagate"])

(defun evs53--word (i)
  (aref evs53--vocab (% i (length evs53--vocab))))

(defun evs53--one (prefix cursor)
  (let* ((t0 (float-time))
         (res (enca-evs-complete prefix cursor))
         (t2 (float-time)))
    (list (* (- t2 t0) 1000.0) (nth 1 res) (nth 2 res))))

(defun evs53--report (name ops exact ext miss lats)
  (let* ((p (evs53--pcts lats))
         (avoided (/ (* 100.0 (+ exact ext)) (max 1 (+ exact ext miss)))))
    (evs53--emit
     (format "TYPING|%s|ops=%d|exact=%d|extend=%d|miss=%d|avoided=%.1f%%|\
p50=%.3f|p95=%.3f|max=%.3f"
             name ops exact ext miss avoided
             (nth 0 p) (nth 1 p) (nth 2 p)))))

(defun evs53--typing-cell ()
  (enca-evs-start 1 nil 'loopback)
  (with-current-buffer (get-buffer-create "*evs53*")
    (erase-buffer)
    (insert (make-string 8000 ?x)))
  (let ((lats nil) (exact 0) (ext 0) (miss 0) (i 0))
    (dotimes (_ 32)
      (let* ((w (evs53--word (/ i 4)))
             (n (1+ (% i (length w))))
             (prefix (substring w 0 n))
             (r (evs53--one prefix 4000)))
        (setq i (1+ i))
        (push (nth 0 r) lats)
        (pcase (nth 2 r)
          ('hit  (if (>= n 4) (setq ext (1+ ext)) (setq exact (1+ exact))))
          ('miss (setq miss (1+ miss)))
          (_     (setq miss (1+ miss))))))
    (evs53--report "identifier-growth" i exact ext miss lats))
  (enca-evs-stop))

(defun evs53--retry-cell ()
  "Identical request repeated; first op populates the cache."
  (enca-evs-start 1 nil 'loopback)
  (with-current-buffer (get-buffer-create "*evs53*")
    (erase-buffer)
    (insert (make-string 8000 ?y)))
  ;; prime
  (enca-evs-complete "print" 4005)
  (let ((lats nil) (exact 0) (ext 0) (miss 0))
    (dotimes (_ 10)
      (let ((r (evs53--one "print" 4005)))
        (push (nth 0 r) lats)
        (if (eq (nth 2 r) 'hit) (setq exact (1+ exact)) (setq miss (1+ miss)))))
    (evs53--report "retry" 10 exact ext miss lats))
  (enca-evs-stop))

(defun evs53--edit-mixed-cell ()
  "Completions interleaved with edits (revision bumps => misses)."
  (enca-evs-start 1 nil 'loopback)
  (with-current-buffer (get-buffer-create "*evs53*")
    (erase-buffer)
    (insert (make-string 8000 ?z)))
  (let ((lats nil) (exact 0) (ext 0) (miss 0) (i 0))
    (dotimes (_ 12)
      (setq i (1+ i))
      (let ((prefix (format "sym%03d" (% i 300)))
            (anchor (+ 100 (* i 50))))
        (let ((r (evs53--one prefix anchor)))
          (push (nth 0 r) lats)
          (if (eq (nth 2 r) 'hit) (setq exact (1+ exact)) (setq miss (1+ miss))))
        ;; simulated edit: bump revision by re-completing after change
        (enca-evs-bump-revision)))
    (evs53--report "edit-interleaved" i exact ext miss lats))
  (enca-evs-stop))

(message "=== EVS-53 BEGIN ===")
(setq evs53--log nil)
(condition-case err
    (progn
      (evs53--typing-cell)
      (evs53--retry-cell)
      (evs53--edit-mixed-cell)
      (message "=== EVS-53 OK ==="))
  (error (message "EVS53 ERROR: %S" err)))
(let ((file (or (getenv "EVS53_LOG") "/tmp/evs53_out.txt")))
  file)
(kill-emacs 0)
