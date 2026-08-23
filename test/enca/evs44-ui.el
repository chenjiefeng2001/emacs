;;; evs44-ui.el --- EVS-4.4 UI attribution harness (tty emacs).  -*- lexical-binding: t; -*-

;; Runs under a REAL terminal (pty): `script -qec "emacs -nw ..." /dev/null`
;; Measures, in live Emacs:
;;   CTAB|count|len|ms      completion-table build + all-completions (T6->T7)
;;   REDIS|rows|p50/p95/max forced redisplay with popup overlay (T9->T11)
;; Results go to BOTH the echo area and the EVS44_LOG file (echo-area
;; overwrites lose intermediate lines on a tty).

(defvar evs44--log nil)

(defun evs44--emit (line)
  (push line evs44--log)
  (message "%s" line))

(defun evs44--rand-label (len j)
  "Deterministic label of LEN bytes, varied by J (no RNG cost)."
  (let ((s (make-string len ?a)))
    (dotimes (i len)
      (aset s i (+ ?a (% (+ (* i 7) (* j 31)) 26))))
    s))

(defun evs44--pct (l p)
  (let ((s (sort (copy-sequence l) #'<)))
    (nth (min (1- (length s)) (floor (* (/ p 100.0) (length s)))) s)))

(defun evs44--ctab-cell (count len reps)
  "Build a completion table (LIST) of COUNT strings of LEN bytes,
then run ALL-COMPLETIONS against prefix \"fa\".  GC runs BEFORE the
timed window."
  (let ((cands nil)
        lats
        res0)
    (dotimes (i count)
      (let ((lb (evs44--rand-label len i)))
        (aset lb 0 ?f)
        (push lb cands)))
    (dotimes (r reps)
      (garbage-collect)
      (let* ((t1 (float-time))
             (res (all-completions "fa" cands))
             (t2 (float-time)))
        (push (* (- t2 t1) 1000.0) lats)
        (when (= r 0)
          (setq res0 (length res)))))
    (let ((sorted (sort (copy-sequence lats) #'<)))
      (evs44--emit (format "CTAB|%d|%d|p50=%.4f|p95=%.4f|max=%.4f|kept=%d"
                           count len
                           (evs44--pct sorted 50)
                           (evs44--pct sorted 95)
                           (car (last sorted))
                           res0)))))

(defun evs44--popup-rows (rows width)
  "Build an after-string simulating a ROWS-line popup."
  (with-temp-buffer
    (dotimes (i rows)
      (let ((lab (evs44--rand-label (max 8 (- width 6)) i)))
        (insert lab)
        (insert (make-string (max 1 (- width (length lab))) ?\s))
        (insert " [fn]"))
      (when (< i (1- rows))
        (insert "\n")))
    (buffer-string)))

(defun evs44--redisplay-cell (rows reps)
  "Install a ROWS-line popup overlay after point and force redisplay
REPS times; report per-redisplay ms (T9..T11)."
  (let ((ov (make-overlay (point) (+ (point) 5)))
        lats
        (tmpl (evs44--popup-rows rows 40)))
    (dotimes (r reps)
      (overlay-put ov 'after-string tmpl)
      (let ((t0 (float-time)))
        (redisplay t)
        (push (* (- (float-time) t0) 1000.0) lats)
        ;; vary content so each redisplay has real work to do
        (setq tmpl (concat tmpl " "))
        (delete-overlay ov)
        (setq ov (make-overlay (point) (+ (point) 5)))))
    (let ((sorted (sort (copy-sequence lats) #'<)))
      (evs44--emit (format "REDIS|%d|p50=%.4f|p95=%.4f|max=%.4f"
                           rows
                           (evs44--pct sorted 50)
                           (evs44--pct sorted 95)
                           (car (last sorted)))))))

(defun evs44--run ()
  ;; T6->T7 matrix; grid trimmed to bound generation cost.
  (dolist (spec '((10 8 32 128 512)
                  (100 8 32 128 512)
                  (1000 8 32)
                  (10000 8 32)
                  (100000 8)))
    (let* ((count (car spec))
           (lens (cdr spec))
           (reps (cond ((<= count 100) 5)
                       ((<= count 10000) 3)
                       (t 2))))
      (dolist (len lens)
        (evs44--ctab-cell count len reps))))
  ;; R-ladder in a live window.
  (switch-to-buffer (get-buffer-create "*evs44*"))
  (erase-buffer)
  (insert (make-string 200000 ?x))
  (goto-char (/ (point-max) 2))
  (redisplay t)
  (dolist (rows '(1 10 50))
    (evs44--redisplay-cell rows 8))
  ;; R0 baseline: forced redisplay without any popup.
  (let ((lats nil))
    (dotimes (i 8)
      (let ((t0 (float-time)))
        (redisplay t)
        (push (* (- (float-time) t0) 1000.0) lats)))
    (let ((sorted (sort (copy-sequence lats) #'<)))
      (evs44--emit (format "REDIS|R0|p50=%.4f|p95=%.4f|max=%.4f"
                           (evs44--pct sorted 50)
                           (evs44--pct sorted 95)
                           (car (last sorted)))))))

(message "=== EVS-44 BEGIN ===")
(setq evs44--log nil)
(condition-case err
    (progn (evs44--run)
           (message "=== EVS-44 OK ==="))
  (error (message "EVS44 ERROR: %S" err)))
(let ((file (or (getenv "EVS44_LOG") "/tmp/evs44_out.txt")))
  (with-temp-file file
    (dolist (l (nreverse evs44--log))
      (insert l "\n"))))
(kill-emacs 0)
