;;; p0-bench-tty.el --- P0 baseline redisplay suite (pty emacs -nw).  -*- lexical-binding: t; -*-

;; Prints:  P0T|cell|rep|ms

(defun p0--popup-rows (rows width)
  (with-temp-buffer
    (dotimes (i rows)
      (let ((lab (make-string (max 8 (- width 6)) ?a)))
        (insert lab (make-string 2 ?\s) "[fn]"))
      (when (< i (1- rows)) (insert "\n")))
    (buffer-string)))

(defun p0--emit (line)
  (message "%s" line)
  (with-temp-buffer
    (insert line "\n")
    (append-to-file (point-min) (point-max) "/tmp/p0_tty.log")))

(defun p0--run-cell (name fn reps)
  (let ((lats nil))
    (dotimes (r reps)
      (let ((t0 (float-time)))
        (funcall fn)
        (push (* (- (float-time) t0) 1000.0) lats)
        (p0--emit (format "P0T|%s|%d|%.4f" name r (* (- (float-time) t0) 1000.0)))))))

(defun p0--run ()
  (write-region "" nil "/tmp/p0_tty.log")
  (switch-to-buffer (get-buffer-create "*p0*"))
  (erase-buffer)
  (insert (make-string 200000 ?x))
  (goto-char (/ (point-max) 2))
  (redisplay t)

  ;; R0: forced no-change redisplay
  (p0--run-cell "R0-noop" (lambda () (redisplay t)) 10)

  ;; R1: small text change + redisplay
  (p0--run-cell "R1-text-edit"
                (lambda ()
                  (insert "zz")
                  (delete-char 2)
                  (redisplay t))
                10)

  ;; R2: popup overlay after-string, 10 rows
  (let ((ov (make-overlay (point) (+ (point) 5)))
        tmpl)
    (unwind-protect
        (progn
          (setq tmpl (p0--popup-rows 10 40))
          (p0--run-cell "R2-popup-10"
                        (lambda ()
                          (overlay-put ov 'after-string
                                       (concat tmpl " "))
                          (redisplay t))
                        10))
      (delete-overlay ov))))

(p0--run)
(message "=== P0 TTY END ===")
(kill-emacs 0)

;;; p0-bench-tty.el ends here
