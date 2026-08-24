;;; evs52-ui.el --- EVS-5.2.6 real UI integration benchmark.  -*- lexical-binding: t; -*-

;; Runs under a REAL terminal (pty).  For each arm the full user path
;; is exercised:
;;
;;   prefix change (keypress) -> enca-evs-complete (capture/snapshot/
;;   scheduler/cache-or-backend/wakeup) -> candidates -> popup overlay
;;   install -> forced redisplay -> VISIBLE
;;
;; Arms:
;;   HIT   same prefix every keystroke      (cache hit expected)
;;   MISS  fresh pseudo-random prefixes     (backend round trip)
;;   MIX   alternating H/M pattern
;;
;; Prints per op:
;;   KPV|arm|engine_ms|ui_ms|total_ms|source
;; and a summary:
;;   KPVSUM|arm|n|hit_rate|eng_p50|vis_p50|vis_p95|vis_max


(defun evs52--pct (l p)
  (let ((s (sort (copy-sequence l) #'<)))
    (nth (min (1- (length s)) (floor (* (/ p 100.0) (length s)))) s)))

(defun evs52--emit (line)
  (message "%s" line)
  (with-temp-buffer
    (insert line "\n")
    (append-to-file (point-min) (point-max)
                    (or (getenv "EVS52_LOG") "/tmp/evs52_out.txt"))))

(defvar evs52--overlay nil)

(defun evs52--popup-rows (cands rows width)
  "Render CANDS into a ROWS-line popup after-string."
  (with-temp-buffer
    (let ((n (min rows (length cands))))
      (dotimes (i n)
        (let* ((lab (nth i cands))
               (pad (max 0 (- width (length lab)))))
          (insert lab (make-string pad ?\s) " [fn]"))
        (when (< i (1- n)) (insert "\n"))))
    (buffer-string)))

(defun evs52--install-popup (cands rows width)
  "Install popup overlay for CANDS; returns ms spent (UI part 1)."
  (let* ((t0 (float-time))
         (ov evs52--overlay)
         (str (evs52--popup-rows cands rows width)))
    (move-overlay ov (point) (+ (point) 3))
    (overlay-put ov 'after-string str)
    (* (- (float-time) t0) 1000.0)))

(defun evs52--visible ()
  "Force redisplay; returns ms spent (T10..T11)."
  (let ((t0 (float-time)))
    (redisplay t)
    (* (- (float-time) t0) 1000.0)))

(defun evs52--arm-run (arm ops prefix-fn cursor-fn rows)
  "Run OPS keystrokes of ARM.  Returns plist of stats."
  (let ((lats nil) (hits 0) (misses 0) (nobd 0) (avoided 0))
    (dotimes (i ops)
      (let* ((prefix (funcall prefix-fn i))
             (cursor (+ (point) (length prefix)))
             (t0 (float-time))
             (res (enca-evs-complete prefix cursor))
             (t1 (float-time))          ; engine done
             (cands (nth 0 res))
             (eng-ms (nth 1 res))
             (src (nth 2 res))
             ;; UI: install + visible
             (ui1 (evs52--install-popup cands rows 36))
             (ui2 (evs52--visible))
             (t2 (float-time))
             (total (* (- t2 t0) 1000.0)))
        (cond ((eq src 'hit)  (setq hits (1+ hits))   (setq avoided (1+ avoided)))
              ((eq src 'miss) (setq misses (1+ misses)))
              (t              (cl-incf nobd)))
        (push total lats)
        (evs52--emit (format "KPV|%s|%.4f|%.4f|%.4f|%s"
                             arm eng-ms (+ ui1 ui2) total src))))
    (let ((sorted (sort (copy-sequence lats) #'<))
          (n (length lats)))
      (evs52--emit
       (format "KPVSUM|%s|ops=%d|hit%%=%.1f|miss=%d|eng_p50=%.4f|\
vis_p50=%.4f|vis_p95=%.4f|vis_max=%.4f"
               arm n (/ (* 100.0 hits) (max 1 n)) misses
               (evs52--pct sorted 50) (evs52--pct sorted 50)
               (evs52--pct sorted 95) (car (last sorted)))))))

(defun evs52--run ()
  (random 42)
  ;; ---- HIT arm: identical prefix, revision constant -------------
  (enca-evs-start 1 nil 'loopback)
  (with-current-buffer (get-buffer-create "*evs52*")
    (erase-buffer)
    (insert "(defun bench-fn ()\n  (progn \n")
    (goto-char (point-max)))
  (setq evs52--overlay (make-overlay (point) (+ (point) 3)))
  ;; prime once so first measured op is a genuine hit
  (enca-evs-complete "print" (+ (point) 5))
  (evs52--arm-run "HIT" 20 (lambda (_i) "print")
                  (lambda (_i) (+ (point) 5)) 10)
  (enca-evs-stop)

  ;; ---- MISS arm: fresh prefixes, real backend -------------------
  (enca-evs-start 1 nil (quote loopback))
  (switch-to-buffer (get-buffer-create "*evs52*"))
  (goto-char (point-max))
  (setq evs52--overlay (make-overlay (point) (+ (point) 3)))
  (let ((lats nil) (n 12))
    (dotimes (i n)
      (let* ((prefix (format "sym%03d" i))
             (cursor (+ (point) (length prefix)))
             (t0 (float-time))
             (res (enca-evs-complete prefix cursor))
             (t1 (float-time))
             (cands (nth 0 res))
             (eng (nth 1 res))
             (src (nth 2 res))
             (u1 (evs52--install-popup cands 10 36))
             (u2 (evs52--visible))
             (tot (* (- t1 t0) 1000.0)))
        (push tot lats)
        (evs52--emit (format "KPV|MISS|%d|%.4f|%.4f|%.4f|%s"
                             i eng (+ u1 u2) tot src))
        (when (= i 0)
          (evs52--emit (format "MISSSAMPLE|first_total=%.3f" tot)))))
    (let ((sorted (sort (copy-sequence lats) #'<)))
      (evs52--emit (format "KPVSUM|MISS|ops=%d|hit%%=0.0|miss=%d|\
eng_p50=n/a|vis_p50=%.4f|vis_p95=%.4f|vis_max=%.4f"
                           n n (evs52--pct sorted 50)
                           (evs52--pct sorted 95) (car (last sorted))))))
  (enca-evs-stop)

  ;; ---- MIX arm: alternating hit / miss --------------------------
  (enca-evs-start 1 nil (quote loopback))
  (switch-to-buffer (get-buffer-create "*evs52*"))
  (goto-char (point-max))
  (setq evs52--overlay (make-overlay (point) (+ (point) 3)))
  (let ((lats nil) (hits 0) (n 16))
    (dotimes (i n)
      (let* ((hitp (= (% i 2) 0))
             (prefix (if hitp "mixed" (format "mix%02d" i)))
             (cursor (+ (point) (length prefix)))
             (t0 (float-time))
             (res (enca-evs-complete prefix cursor))
             (t1 (float-time))
             (cands (nth 0 res))
             (eng (nth 1 res))
             (src (nth 2 res))
             (u1 (evs52--install-popup cands 10 36))
             (u2 (evs52--visible))
             (tot (* (- t1 t0) 1000.0)))
        (when hitp (setq hits (1+ hits)))
        (push tot lats)
        (evs52--emit (format "KPV|MIX|%d|%.4f|%.4f|%.4f|%s"
                             i eng (+ u1 u2) tot src))))
    (let ((sorted (sort (copy-sequence lats) #'<)))
      (evs52--emit (format "KPVSUM|MIX|ops=%d|hit%%=%.1f|miss=%d|\
eng_p50=n/a|vis_p50=%.4f|vis_p95=%.4f|vis_max=%.4f"
                           n (/ (* 100.0 hits) n) (- n hits)
                           (evs52--pct sorted 50)
                           (evs52--pct sorted 95)
                           (car (last sorted))))))
  (enca-evs-stop))

(message "=== EVS-52 BEGIN ===")
(condition-case err
    (evs52--run)
  (error (message "EVS52 ERROR: %S" err)))
(let ((file (or (getenv "EVS52_LOG") "/tmp/evs52_out.txt")))
  (message "=== EVS-52 END ===")
  file)                                  ; log already flushed per emit
(kill-emacs 0)
