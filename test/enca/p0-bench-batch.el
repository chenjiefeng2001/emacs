;;; p0-bench-batch.el --- P0 baseline batch suite (portable).  -*- lexical-binding: t; -*-

;; Identical workload on vanilla / ENCA-disabled / ENCA-enabled builds.
;; Prints:  P0B|cell|rep|ms

(defun p0--now () (float-time))

(defun p0--run-cell (name fn reps)
  (let ((lats nil))
    (dotimes (r reps)
      (garbage-collect)
      (let* ((t0 (p0--now))
             (_ (funcall fn))
             (t1 (p0--now)))
        (push (* (- t1 t0) 1000.0) lats)
        (message "P0B|%s|%d|%.4f" name r (* (- t1 t0) 1000.0))))
    lats))

(defun p0--buffer-insert ()
  (with-temp-buffer
    (insert (make-string 1000000 ?x))
    (goto-char (/ (point-max) 2))
    (insert "hello world")
    (buffer-size)))

(defun p0--buffer-replace ()
  (with-temp-buffer
    (insert (make-string 200000 ?a))
    (dotimes (i 200)
      (goto-char (+ 100 (* i 900)))
      (delete-char 8)
      (insert "replaced!"))
    (buffer-size)))

(defun p0--sort-10k ()
  (let ((v (make-vector 10000 nil)))
    (dotimes (i 10000) (aset v i (% (* i 7919) 10000)))
    (setq v (sort v #'<))
    (length v)))

(defun p0--string-ops ()
  (let ((acc "")
        (parts (make-vector 500 "segment-")))
    (dotimes (i 500)
      (setq acc (concat acc (aref parts i) (number-to-string i))))
    (length acc)))

(defun p0--completion-10k ()
  (let ((cands nil))
    (dotimes (i 10000)
      (let ((lb (make-string 16 ?a)))
        (aset lb 0 ?f)
        (aset lb 5 (+ ?a (% i 26)))
        (push lb cands)))
    (length (all-completions "fa" cands))))

(defun p0--gc-full ()
  (garbage-collect))

(defun p0--search-1mb ()
  (with-temp-buffer
    (insert (make-string 1048576 ?a))
    (goto-char (point-min))
    (let ((cnt 0))
      (while (re-search-forward "aaaaab" nil t)
        (setq cnt (1+ cnt))
        (when (> cnt 100) (goto-char (point-max))))
      cnt)))

;;; driver -- each cell runs 7 reps, median is the readout
(message "=== P0 BATCH BEGIN ===")
(p0--run-cell "buffer-insert-1MB"   #'p0--buffer-insert 7)
(p0--run-cell "buffer-replace-200"  #'p0--buffer-replace 7)
(p0--run-cell "sort-10k"            #'p0--sort-10k 7)
(p0--run-cell "string-concat-500"   #'p0--string-ops 7)
(p0--run-cell "completion-10k"      #'p0--completion-10k 7)
(p0--run-cell "regexp-search-1MB"   #'p0--search-1mb 7)
(p0--run-cell "gc-full"             #'p0--gc-full 7)
(message "=== P0 BATCH END ===")
