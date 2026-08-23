;;; evs1-closure.el --- EVS-1 closure: B0/B1/B2 + attribution.  -*- lexical-binding: t; -*-

(require 'seq)

(defun evs1c--pct (sorted p)
  (nth (min (1- (length sorted))
            (floor (* (/ p 100.0) (length sorted))))
       sorted))

(defun evs1c--summary (name lats)
  (let* ((s (sort (copy-sequence lats) #'<))
         (n (length s)))
    (list name n
          (evs1c--pct s 50) (evs1c--pct s 95)
          (evs1c--pct s 99) (evs1c--pct s 999))))

(defun evs1c--report-row (name lats)
  (let ((r (evs1c--summary name lats)))
    (message "LAT|%s|n=%d|p50=%.4f|p95=%.4f|p99=%.4f|max=%.4f"
             (nth 0 r) (nth 1 r) (nth 2 r) (nth 3 r) (nth 4 r) (nth 5 r))
    r))

;;; ---------- B0: native Emacs insert only ----------

(defun evs1c--b0 (&optional n)
  (enca-evs-start 2)
  (with-temp-buffer
    (setq n (or n 500))
    (let ((lats ()))
      (dotimes (i n)
        (let ((t0 (float-time)))
          (insert "x")
          (push (* (- (float-time) t0) 1000.0) lats)))
      (evs1c--report-row "B0-native-insert" (nreverse lats))))
  (enca-evs-stop))

;;; ---------- B1: synchronous analysis (no scheduler/worker) ----------

(defun evs1c--b1 (&optional n)
  (enca-evs-start 2)
  (with-temp-buffer
    (insert "seed")
    (setq n (or n 500))
    (let ((lats ()))
      (dotimes (i n)
        (insert "x")
        ;; Capture + snapshot + FNV synchronously on THIS thread.
        (let* ((str (buffer-string))
               (t0 (float-time))
               (res (enca-evs-sync-analysis str))
               (dt (- (float-time) t0)))
          (unless (consp res) (error "B1 sync failed"))
          (push (* dt 1000.0) lats)))     ; float-time is sec -> ms
      (evs1c--report-row "B1-sync-analysis" (nreverse lats))))
  (enca-evs-stop))

;;; ---------- B2: ENCA async path, tight-spin pump ----------

(defvar evs1c--spin-done)

(defun evs1c--b2 (&optional n)
  (enca-evs-start 2)
  (with-temp-buffer
    (insert "seed")
    (setq n (or n 500))
    (let ((lats ()))
      (dotimes (i n)
        (insert "x")
        (let* ((target (1+ i))
               (rec nil)
               (t-submit (float-time)))
          ;; Tight-spin pump: zero sleep so we measure the true path.
          (while (not rec)
            (enca-evs-pump)
            (setq rec (enca-evs-latency 0))
            (when (and rec (/= (nth 0 rec) target)) (setq rec nil)))
          ;; Precise latency from the C-side ns ring.
          (push (/ (- (nth 2 rec) (nth 1 rec)) 1000000.0) lats)))
      (evs1c--report-row "B2-enca-async" (nreverse lats))
      (message "CAPTURE|%s" (enca-evs-capture-stats))))
  (enca-evs-stop))

;;; ---------- E4 attribution (10MB, per-phase breakdown) ----------

(defun evs1c--e4-attribution (&optional size edits)
  (setq size (or size 10485760) edits (or edits 10))
  (enca-evs-start 2)
  (with-temp-buffer
    (insert (make-string size ?a))
    (goto-char (/ size 2))
    (dotimes (i edits)
      (let* ((t-key (float-time))
             (str (buffer-string))
             (t-captured (float-time))
             (_ (enca-evs-on-change str))
             ;; wait for commit via tight pump
             (_ (progn
                  (while (< (or (enca-evs-last-commit) 0) i)
                    (enca-evs-pump))
                  (float-time))))
      ;; attribution from C accounting:
      (message "E4ATTR|edit=%d|capture_avg_ms=%.4f|total_capture_ms=%.2f"
               i
               (nth 2 (enca-evs-capture-stats))
               (nth 0 (enca-evs-capture-stats))))))
  (enca-evs-stop))

;;; ---------- driver ----------


(message "=== EVS-1 CLOSURE BEGIN ===")
(condition-case err (evs1c--b0 500)
  (error (message "B0 ERROR: %S" err)))
(condition-case err (evs1c--b1 500)
  (error (message "B1 ERROR: %S" err)))
(condition-case err (evs1c--b2 500)
  (error (message "B2 ERROR: %S" err)))
(condition-case err (evs1c--e4-attribution 10485760 8)
  (error (message "E4ATTR ERROR: %S" err)))
(message "=== EVS-1 CLOSURE END ===")
