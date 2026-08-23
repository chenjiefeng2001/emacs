;;; evs1-bench.el --- EVS-1 vertical-slice benchmarks (batch).  -*- lexical-binding: t; -*-

;; Runs E1/E3/E4 against the ENCA interactive path and prints one CSV
;; row per scenario plus percentile summaries.

(defun evs1--pct (sorted p)
  (nth (min (1- (length sorted))
            (floor (* (/ p 100.0) (length sorted))))
       sorted))

(defun evs1--summary (name lats extra)
  (let* ((s (sort (copy-sequence lats) #'<))
         (n (length s)))
    (message "SUMMARY|%s|%d|%.4f|%.4f|%.4f|%.4f|%s"
             name n
             (evs1--pct s 50) (evs1--pct s 95)
             (evs1--pct s 99) (or (car (last s)) 0)
             extra)))

(defun evs1--wait-committed (target-rev max-ms)
  "Pump until last-commit >= TARGET-REV or MAX-MS elapsed.
Returns elapsed ms or nil on timeout."
  (let ((t0 (float-time))
        (deadline (+ (float-time) (/ max-ms 1000.0))))
    (while (and (< (float-time) deadline)
                (or (null (enca-evs-last-commit))
                    (< (enca-evs-last-commit) target-rev)))
      (enca-evs-pump)
      (sleep-for 0 1))
    (when (and (enca-evs-last-commit)
               (>= (enca-evs-last-commit) target-rev))
      (* (- (float-time) t0) 1000.0))))

(defun evs1--e1-idle-typing (&optional n)
  "E1: type one char, wait for its commit, measure submit->commit."
  (enca-evs-start 2)
  (with-temp-buffer
    (let ((lats ()))
      (dotimes (i n)
        (insert "x")
        (let ((r (enca-evs-on-change (buffer-string))))
          (unless (memq r '(accepted replaced)) (error "E1 admit=%S" r)))
        (let ((dt (evs1--wait-committed (1+ i) 5000)))
          (unless dt (error "E1 timeout at %d" i))
          (push dt lats)))
      (evs1--summary "E1-idle-typing" (nreverse lats)
                     (format "workers=2")))
    (enca-evs-pump))
  (enca-evs-stop))

(defun evs1--e3-storm (&optional n workers)
  "E3: revision storm -- N rapid edits with NO pumping in between."
  (enca-evs-start workers)
  (with-temp-buffer
    (insert "base")
    (let ((submitted 0))
      (dotimes (i n)
        (insert "x")
        (let ((r (enca-evs-on-change (buffer-string))))
          (if (memq r '(accepted replaced)) (setq submitted (1+ submitted)))))
      ;; Single pump: admission should have killed all but the latest.
      (let ((deadline (+ (float-time) 5.0))
            (last-rev nil))
        (while (< (float-time) deadline)
          (enca-evs-pump)
          (sleep-for 0 2)
          (when (and (enca-evs-last-commit)
                     (eq last-rev (enca-evs-last-commit))
                     (>= last-rev n))
            (setq deadline 0))          ; converged: same commit twice
          (setq last-rev (enca-evs-last-commit)))
        (let* ((st (enca-evs-stats))
               (sub (nth 0 st)) (exec (nth 1 st))
               (waste (nth 2 st)) (sup (nth 3 st)))
          (message "STORM|%d|submitted=%d|executed=%d|superseded=%d|wasted=%d|committed_rev=%s"
                   n submitted exec sup waste (enca-evs-last-commit)))))
    (setcdr (cdr (cdr (cdr (enca-evs-stats)))) nil))
  (enca-evs-stop))

(defun evs1--e4-large (&optional size edits)
  "E4: large buffer, local edit near middle, measure capture+commit."
  (enca-evs-start 2)
  (with-temp-buffer
    (insert (make-string size ?a))
    (goto-char (/ size 2))
    (let ((lats ()))
      (dotimes (i edits)
        (insert "hello")                       ; small local edit
        (let ((r (enca-evs-on-change (buffer-string))))
          (unless (memq r '(accepted replaced)) (error "E4 admit=%S" r)))
        (let ((dt (evs1--wait-committed (+ edits 0) 20000)))
          (when dt (push dt lats))))
      (evs1--summary (format "E4-large-%dKB" (/ size 1024))
                     (nreverse lats) (format "edits=%d" edits)))
    (enca-evs-pump))
  (enca-evs-stop))

;;; Driver

(message "=== EVS-1 BEGIN ===")
(condition-case err (evs1--e1-idle-typing 200)
  (error (message "E1 ERROR: %S" err)))
(condition-case err (evs1--e3-storm 300 2)
  (error (message "E3 ERROR: %S" err)))
(condition-case err (evs1--e4-large 1048576 30)
  (error (message "E4-1MB ERROR: %S" err)))
(condition-case err (evs1--e4-large 10485760 20)
  (error (message "E4-10MB ERROR: %S" err)))
(message "=== EVS-1 END ===")
