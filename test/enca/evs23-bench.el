;;; evs23-bench.el --- EVS-2.3 A/B: full vs incremental capture.  -*- lexical-binding: t; -*-

;; Runs E1/E3/E4 through the SAME pipeline twice -- arm A (full
;; buffer-string capture) and arm B (incremental byte-range capture)
;; -- and prints machine-parseable rows:
;;
;;   SUMMARY|MODE|NAME|extra|n|p50|p95|p99|max      (latencies, ms)
;;   CELL|MODE|size_kb|es|edits|cap_avg_ms|wasted   (per E4 cell)
;;
;; Environment knobs:
;;   EVS23_MODES   comma list subset of "full,incr"  (default both)
;;   EVS23_MAX_MB  skip E4 cells larger than this    (default 100)
;;
;; Methodology note: submit->commit latency includes the batch poll
;; granularity, identical across arms, so the A/B delta isolates the
;; capture strategy (the only thing EVS-2.3 is allowed to change).

(defun evs23--pct (sorted p)
  (nth (min (1- (length sorted))
            (floor (* (/ p 100.0) (length sorted))))
       sorted))

(defun evs23--summary (mode name lats extra)
  (let* ((s (sort (copy-sequence lats) #'<))
         (n (length s)))
    (message "SUMMARY|%s|%s|%s|%d|%.4f|%.4f|%.4f|%.4f"
             mode name extra n
             (evs23--pct s 50) (evs23--pct s 95)
             (evs23--pct s 99) (or (car (last s)) 0))))

(defun evs23--wait-committed (target-rev max-ms)
  "Block until LAST-COMMIT >= TARGET-REV or MAX-MS elapsed.
Uses the EVS-3 wakeup primitive when available (sub-ms floor);
otherwise falls back to the legacy sleep-poll loop.
Returns elapsed ms or nil on timeout."
  (if (fboundp 'enca-evs-wait-committed)
      (enca-evs-wait-committed target-rev max-ms)
    (let ((t0 (float-time))
          (deadline (+ (float-time) (/ max-ms 1000.0))))
      (while (and (< (float-time) deadline)
                  (or (null (enca-evs-last-commit))
                      (< (enca-evs-last-commit) target-rev)))
        (enca-evs-pump)
        (sleep-for 0 1))
      (when (and (enca-evs-last-commit)
                 (>= (enca-evs-last-commit) target-rev))
        (* (- (float-time) t0) 1000.0)))))

(defun evs23--modes ()
  (let ((raw (or (getenv "EVS23_MODES") "full,incr")))
    (mapcar #'intern (split-string raw "," t "[ \t]+"))))

(defun evs23--max-mb ()
  (let ((v (getenv "EVS23_MAX_MB")))
    (if v (string-to-number v) 100)))

(defun evs23--start (mode workers)
  (if (eq mode 'incr)
      (enca-evs-start workers 'incremental)
    (enca-evs-start workers)))

(defun evs23--e1 (&optional mode n)
  "E1: type one char at a time, wait for each commit."
  (setq mode (or mode 'full) n (or n 200))
  (evs23--start mode 2)
  (with-temp-buffer
    (let ((lats ()) (len 0))
      (dotimes (i n)
        (let ((pos len))            ; append position in base revision
          (insert "x")
          (setq len (1+ len))
          (let ((r (if (eq mode 'incr)
                       (enca-evs-on-change-delta pos pos "x")
                     (enca-evs-on-change (buffer-string)))))
            (unless (memq r '(accepted replaced))
              (error "E1 admit=%S" r))))
        (let ((dt (evs23--wait-committed (1+ i) 5000)))
          (unless dt (error "E1 timeout at %d" i))
          (push dt lats)))
      (evs23--summary mode "E1-idle-typing" (nreverse lats) "workers=2"))
    (enca-evs-pump))
  (enca-evs-stop))

(defun evs23--e3 (&optional mode n workers)
  "E3: revision storm -- N rapid edits, NO pumping in between."
  (setq mode (or mode 'full) n (or n 300) workers (or workers 2))
  (evs23--start mode workers)
  (with-temp-buffer
    (insert "base")
    (let ((submitted 0) (len 5))
      (dotimes (i n)
        (let ((pos len))            ; append position in base revision
          (insert "x")
          (setq len (1+ len))
          (let ((r (if (eq mode 'incr)
                       (enca-evs-on-change-delta pos pos "x")
                     (enca-evs-on-change (buffer-string)))))
            (if (memq r '(accepted replaced))
                (setq submitted (1+ submitted))))))
      (let ((deadline (+ (float-time) 5.0))
            (last-rev nil))
        (while (< (float-time) deadline)
          (enca-evs-pump)
          (sleep-for 0 2)
          (when (and (enca-evs-last-commit)
                     (eq last-rev (enca-evs-last-commit))
                     (>= last-rev n))
            (setq deadline 0))
          (setq last-rev (enca-evs-last-commit)))
        (let* ((st (enca-evs-stats))
               (sub (nth 0 st)) (exec (nth 1 st))
               (sup (nth 3 st)) (waste (nth 2 st)))
          (message "STORM|%s|%d|submitted=%d|executed=%d|superseded=%d|wasted=%d|committed_rev=%s"
                   mode n submitted exec sup waste (enca-evs-last-commit)))))
    (enca-evs-pump))
  (enca-evs-stop))

(defun evs23--e4-cell (mode size es edits)
  "One E4 cell: SIZE-byte buffer, local ES-byte insert near middle."
  (evs23--start mode 2)
  (with-temp-buffer
    (let* ((ins (make-string es ?x))
           (mid (/ size 2))
           ;; Seed the document state OUTSIDE the timed region.
           (_ (progn
                (insert (make-string size ?a))
                (when (eq mode 'incr)
                  (let ((r (enca-evs-on-change-delta 0 0
                             (buffer-string))))
                    (unless (memq r '(accepted replaced))
                      (error "E4 seed admit=%S" r)))
                  (unless (evs23--wait-committed 1 120000)
                    (error "E4 seed timeout")))))
           (lats ())
           (t0cap (nth 0 (enca-evs-capture-stats)))
           (ncap0 (nth 1 (enca-evs-capture-stats)))
           (rev0 (or (enca-evs-last-commit) 0)))
      (goto-char mid)
      (dotimes (i edits)
        (insert ins)
        (let ((r (if (eq mode 'incr)
                     (enca-evs-on-change-delta mid mid ins)
                   (enca-evs-on-change (buffer-string)))))
          (unless (memq r '(accepted replaced)) (error "E4 admit=%S" r)))
        (let ((dt (evs23--wait-committed (+ rev0 (1+ i)) 120000)))
          (when dt (push dt lats))))
      (let* ((cs (enca-evs-capture-stats))
             (cap-ms (- (nth 0 cs) t0cap))
             (ncap (- (nth 1 cs) ncap0))
             (avg (if (> ncap 0) (/ cap-ms (float ncap)) 0))
             (st (enca-evs-stats))
             (wasted (nth 2 st)))
        (evs23--summary mode
                        (format "E4-%dKB-es%d" (/ size 1024) es)
                        (nreverse lats)
                        (format "edits=%d" edits))
        (message "CELL|%s|%d|%d|%d|%.4f|%d"
                 mode (/ size 1024) es edits avg wasted))))
  (enca-evs-stop))

(defun evs23--run-e4-matrix (mode)
  (dolist (size (list (* 1 1024 1024) (* 10 1024 1024)
                      (* 100 1024 1024)))
    (when (<= (/ size 1024 1024) (evs23--max-mb))
      (dolist (es '(1 10 100 1024))
        (condition-case err
            (evs23--e4-cell mode size es 12)
          (error (message "E4 ERROR %s sz=%d es=%d: %S"
                          mode size es err)))))))

;;; Driver

(message "=== EVS-23 BEGIN ===")
(dolist (mode (evs23--modes))
  (condition-case err (evs23--e1 mode 200)
    (error (message "E1 ERROR %s: %S" mode err)))
  (condition-case err (evs23--e3 mode 300 2)
    (error (message "E3 ERROR %s: %S" mode err)))
  (condition-case err (evs23--run-e4-matrix mode)
    (error (message "E4 MATRIX ERROR %s: %S" mode err)))
  ;; Copy-amplification attribution for this arm (incr only).
  (let ((ds (enca-evs-delta-stats)))
    (message "DELTA|%s|copied=%d|changed=%d|edits=%d"
             mode (nth 0 ds) (nth 1 ds) (nth 2 ds))))
(message "=== EVS-23 END ===")
