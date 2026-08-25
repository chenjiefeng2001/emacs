;;; eipb_core.el --- EIPB Tier-1 core interactive benchmark.  -*-
;;; lexical-binding: t; -*-

;; Contract: bench/eipb/EIPB.md section 3 T1.  Runs INSIDE a live tty
;; emacs; the shell side measures startup walls separately.  All
;; output goes line-by-line to the file named by EIPB_LOG:
;;
;;   EIPB|<build>|<cell>|<metric>|<value>
;;
;; Latency cells emit n/p50/p95/p99/max rows (milliseconds).

(require 'cl-lib)

(defconst eipb--build (or (getenv "EIPB_BUILD") "UNKNOWN"))
(defvar eipb--overlay nil)

(defun eipb--log-file ()
  (or (getenv "EIPB_LOG") "/tmp/eipb_out.txt"))

(defun eipb--emit (cell metric value)
  (with-temp-buffer
    (insert (format "EIPB|%s|%s|%s|%.4f\n" eipb--build cell metric value))
    (append-to-file (point-min) (point-max) (eipb--log-file))))

(defun eipb--pct (l p)
  (let ((s (sort (copy-sequence l) #'<)) (n (length l)))
    (nth (max 0 (min (1- n) (1- (ceiling (* (/ p 100.0) n))))) s)))

(defun eipb--dist (cell l)
  "Emit n/p50/p95/p99/max rows for L (millisecond latencies)."
  (when l
    (eipb--emit cell "n" (float (length l)))
    (dolist (p '(50 95 99))
      (eipb--emit cell (format "p%d" p) (eipb--pct l p)))
    (eipb--emit cell "max" (car (last (sort (copy-sequence l) #'<))))))

(defun eipb--new-buffer (name unit repeats)
  (switch-to-buffer (get-buffer-create name))
  (setq buffer-undo-list t)
  (erase-buffer)
  (dotimes (_ repeats) (insert unit))
  (current-buffer))

(defun eipb--timed-reps (n fn)
  "Run FN N times; return list of per-call milliseconds."
  (let ((lats nil))
    (dotimes (_ n)
      (let ((t0 (float-time)))
        (funcall fn)
        (push (* (- (float-time) t0) 1000.0) lats)))
    (nreverse lats)))

;; ---------------- EDITING ----------------

(defun eipb--editing ()
  "Editing ladder over three sizes (contract T1 EDITING)."
  (dolist (spec '((5000   "64KB" 300 300 300 100)
                  (80000  "1MB"  300 300 300 100)
                  (800000 "10MB" 150 150 150 30)))
    (let* ((repeats (nth 0 spec))
           (tag     (nth 1 spec))
           (kb      (make-string 1000 ?k)))
      (eipb--new-buffer "*eipb-edit*" "printf_value " repeats)
      (goto-char (point-max))
      ;; ins-eob-1B
      (eipb--dist (format "edit/%s/ins-eob-1B" tag)
                  (eipb--timed-reps (nth 2 spec)
                                    (lambda () (insert "x"))))
      ;; ins-mid-1B
      (let* ((mid (/ (point-max) 2)))
        (eipb--dist (format "edit/%s/ins-mid-1B" tag)
                    (eipb--timed-reps
                     (nth 3 spec)
                     (lambda ()
                       (save-excursion
                         (goto-char mid) (insert "x"))))))
      ;; del-mid-1B
      (let* ((mid (/ (point-max) 2)))
        (eipb--dist (format "edit/%s/del-mid-1B" tag)
                    (eipb--timed-reps
                     (nth 4 spec)
                     (lambda () (delete-region (- mid 1) mid)))))
      ;; paste-1KB @mid
      (let* ((mid (/ (point-max) 2)))
        (eipb--dist (format "edit/%s/paste-1KB" tag)
                    (eipb--timed-reps
                     (nth 5 spec)
                     (lambda ()
                       (save-excursion
                         (goto-char mid) (insert kb))))))
      ;; visible variant on 1MB only: insert + forced redisplay
      (when (string= tag "1MB")
        (goto-char (point-max))
        (eipb--dist "edit/1MB/ins-eob-1B+visible"
                    (eipb--timed-reps
                     200
                     (lambda ()
                       (insert "x")
                       (redisplay t)))))
      (kill-buffer "*eipb-edit*"))))

;; ---------------- REDISPLAY ----------------

(defun eipb--redisplay-section ()
  "R0/R1/R3 on a plain 1MB buffer, R2 popup overlay (tty)."
  (eipb--new-buffer "*eipb-rdisp*" "printf_value " 80000)
  ;; R0 idle noop
  (eipb--dist "rdisp/R0-noop"
              (eipb--timed-reps 200 (lambda () (redisplay t))))
  ;; R1 single-site change + repaint
  (let ((pos 40000))
    (eipb--dist "rdisp/R1-one-change"
                (eipb--timed-reps
                 200
                 (lambda ()
                   (save-excursion
                     (goto-char pos) (delete-char 1) (insert "z"))
                   (redisplay t)))))
  ;; R3 bulk change: 100 scattered sites, ONE repaint
  (let ((seed-state 12345))
    (eipb--dist "rdisp/R3-100-changes"
                (eipb--timed-reps
                 50
                 (lambda ()
                   (dotimes (_ 100)
                     (setq seed-state
                           (% (+ (* seed-state 1103515245) 12345)
                              1073741824))
                     (save-excursion
                       (goto-char (+ 1 (% seed-state (point-max))))
                       (delete-char 1)
                       (insert "q")))
                   (redisplay t)))))
  ;; R2 popup overlay
  (goto-char (point-max))
  (setq eipb--overlay (make-overlay (point) (+ (point) 3)))
  (let ((str (with-temp-buffer
               (dotimes (i 8)
                 (insert (format "candidate-%02d                    [fn]" i))
                 (when (< i 7) (insert "\n")))
               (buffer-string))))
    (eipb--dist "rdisp/R2-popup"
                (eipb--timed-reps
                 100
                 (lambda ()
                   (move-overlay eipb--overlay (point) (+ (point) 3))
                   (overlay-put eipb--overlay 'after-string str)
                   (redisplay t)))))
  (delete-overlay eipb--overlay)
  (kill-buffer "*eipb-rdisp*"))

;; ---------------- GC ----------------

(defun eipb--gc-section ()
  "Forced-GC pause distribution + allocation storm."
  (garbage-collect)
  (eipb--dist "gc/forced-pause"
              (eipb--timed-reps
               200
               (lambda ()
                 (dotimes (_ 50) (make-string 1024 ?.))
                 (garbage-collect))))
  ;; storm: short-lived conses; sample counters around chunks
  (let* ((chunks 200) (per-chunk 100000)
         (g0 gcs-done) (e0 gc-elapsed)
         (t0 (float-time))
         acc)
    (dotimes (_ chunks)
      (dotimes (_ per-chunk)
        (setq acc (cons (cons 1 2) acc))))
    (let* ((wall (- (float-time) t0))
           (gcs (- gcs-done g0))
           ;; NOTE: in this build gc-elapsed advances in SECONDS
           ;; (verified empirically: delta 1.48 over 10 collections),
           ;; despite the manual saying microseconds.
           (gctms (* (- gc-elapsed e0) 1000.0)))
      (eipb--emit "gc/storm" "wall_s" wall)
      (eipb--emit "gc/storm" "gc_count" (float gcs))
      (eipb--emit "gc/storm" "gc_total_ms" gctms)
      (when (> gcs 0)
        (eipb--emit "gc/storm" "gc_mean_pause_ms" (/ gctms gcs)))))
  (garbage-collect))

;; ---------------- SEARCH ----------------

(defun eipb--search-sweep (buf matcher reps tag)
  (let ((results nil))
    (dotimes (_ reps)
      (with-current-buffer buf
        (goto-char (point-min))
        (let ((t0 (float-time)))
          (funcall matcher)
          (push (* (- (float-time) t0) 1000.0) results))))
    (eipb--dist tag results)))

(defun eipb--search-section ()
  "Literal + regexp sweeps over 1MB and 10MB ASCII buffers."
  (dolist (spec '((80000 . "1MB") (800000 . "10MB")))
    (let* ((repeats (car spec)) (tag (cdr spec))
           (buf (get-buffer-create "*eipb-search*")))
      (with-current-buffer buf
        (setq buffer-undo-list t)
        (erase-buffer)
        (dotimes (_ repeats) (insert "printf_value ")))
      (eipb--search-sweep
       buf (lambda ()
             (goto-char (point-min))
             (while (search-forward "printf_value" nil t)))
       (if (string= tag "1MB") 8 3)
       (format "search/%s/literal" tag))
      (eipb--search-sweep
       buf (lambda ()
             (goto-char (point-min))
             (while (re-search-forward "\\<pri[a-z]+\\>" nil t)))
       (if (string= tag "1MB") 5 2)
       (format "search/%s/regexp-id" tag))
      (eipb--search-sweep
       buf (lambda ()
             (goto-char (point-min))
             (while (re-search-forward
                     "\\<\\(printf\\|value\\|rintf\\)\\>" nil t)))
       (if (string= tag "1MB") 5 2)
       (format "search/%s/regexp-alt" tag))
      (with-current-buffer buf
        (eipb--emit (format "search/%s" tag) "size_MB"
                    (/ (* repeats 13.0) 1048576.0)))
      (kill-buffer "*eipb-search*"))))

;; ---------------- UNDO ----------------

(defun eipb--undo-section ()
  "200 rounds of insert+timed-undo on a fresh 1MB buffer.

Measures the interactive unit 'undo my last keystroke'.  Two
invariants found empirically: (1) the undo itself must not be
recorded -- primitive-undo with recording enabled doubles history
per call (O(2^n) explosion); (2) after each timed undo the global
buffer-undo-list must be reset to nil, else stale entries trigger
'outside visible portion' errors on later rounds."
  (eipb--new-buffer "*eipb-undo*" "printf_value " 80000)
  (setq buffer-undo-list nil)
  (let ((seed 424242) (lats nil) (ins-total 0.0))
    (dotimes (_ 200)
      (setq seed (% (+ (* seed 1103515245) 12345) 1073741824))
      (goto-char (+ 1 (% seed (point-max))))
      (let ((ti (float-time)))
        (insert "u")
        (setq ins-total (+ ins-total (- (float-time) ti))))
      (let ((t0 (float-time))
            (ul buffer-undo-list))
        (let ((buffer-undo-list t))
          (primitive-undo 1 ul))
        ;; Drop the consumed head + any stale remainder NOW: letting
        ;; them accumulate makes later primitive-undo calls walk over
        ;; entries whose text is already gone => 'outside visible
        ;; portion' errors forever after (empirically verified).
        (setq buffer-undo-list nil)
        (push (* (- (float-time) t0) 1000.0) lats)))
    (eipb--emit "undo/insert-x200" "total_ms" (* ins-total 1000.0))
    (eipb--dist "undo/undo-single" lats))
  (kill-buffer "*eipb-undo*"))

;; ---------------- main ----------------

(message "=== EIPB CORE BEGIN (%s) ===" eipb--build)
(condition-case err
    (progn
      (eipb--editing)
      (eipb--redisplay-section)
      (eipb--gc-section)
      (eipb--search-section)
      (eipb--undo-section)
      (message "=== EIPB CORE OK ==="))
  (error (message "EIPB ERROR: %S" err)))
(or (getenv "EIPB_LOG") "/tmp/eipb_out.txt")
(kill-emacs 0)
