;;; eipb_p21_core.el --- EIPB Phase 2.1 B/C attribution probe -*- lexical-binding: t; -*-
;;
;; Question (frozen): WHO causes the phase-2 mid-buffer edit+visible
;; D-vs-A gap -- fork base (B~D) or ENCA (C~D)?  Measurement only.
;;
;; Controls:
;;   fundamental-mode everywhere (font-lock excluded by design)
;;   gc-cons-threshold pinned 64MB + explicit GC before every cell
;;     => identical GC state across arms and builds
;;   identical op trace per cell across builds
;;   buffer-undo-list t everywhere
;;
;; Cells:
;;   mb/100KB, mb/1MB   mid-buffer insert+visible  (the SIGNAL)
;;   eob/1MB            EOB insert+visible          (control: was D==A)
;;   noop/1MB           idle redisplay              (control: was D==A)
;;   win8/mb            8 windows, mid-buffer editvis
;;   bufswitch          100-buffer switch cycle
;;
;; Output: EIPB2|<build>|p21/<cell>|<metric>|<value>

(require 'cl-lib)

(defconst eipb2--build (or (getenv "EIPB_BUILD") "UNKNOWN"))

(defun eipb2--log ()
  (or (getenv "EIPB_LOG") "/tmp/eipb_p21_out.txt"))

(defun eipb2--append-line (line)
  (with-temp-buffer
    (insert line)
    (append-to-file (point-min) (point-max) (eipb2--log))))

(defun eipb2--emit (cell metric value)
  (eipb2--append-line
   (format "EIPB2|%s|%s|%s|%.4f\n" eipb2--build cell metric value)))

(defun eipb2--pct (l p)
  (let ((s (sort (copy-sequence l) #'<)) (n (length l)))
    (nth (max 0 (min (1- n) (1- (ceiling (* (/ p 100.0) n))))) s)))

(defun eipb2--dist (cell l)
  (setq l (cl-loop for x in l when (numberp x) collect x))
  (when l
    (eipb2--emit cell "n" (float (length l)))
    (dolist (p '(50 95 99))
      (eipb2--emit cell (format "p%d" p) (eipb2--pct l p)))
    (eipb2--emit cell "max" (car (last (sort (copy-sequence l) #'<))))))

(defun eipb2--timed-reps (n fn)
  (let ((lats nil))
    (dotimes (_ n)
      (let ((t0 (float-time)))
        (funcall fn)
        (push (* (- (float-time) t0) 1000.0) lats)))
    (nreverse lats)))

(defun eipb2--fill-buffer (name bytes unit)
  (switch-to-buffer (get-buffer-create name))
  (setq buffer-undo-list t)
  (fundamental-mode)
  (erase-buffer)
  (let ((i 0))
    (while (< (- (point-max) (point-min)) bytes)
      (insert unit)
      (setq i (1+ i))))
  (current-buffer))

(defvar eipb2--gc-pin (* 64 1024 1024))

(defmacro eipb2--cell (cell reps &rest body)
  "Pin GC state, run BODY REPS times timed, emit dist under CELL."
  (declare (indent 2))
  `(let ((gc-cons-threshold eipb2--gc-pin) (lats nil))
     (garbage-collect)
     (dotimes (_ ,reps)
       (let ((t0 (float-time)))
         ,@body
         (push (* (- (float-time) t0) 1000.0) lats)))
     (eipb2--dist ,cell lats)))

(defun eipb2--set-windows (n)
  (delete-other-windows)
  (while (< (length (window-list)) n)
    (select-window (get-largest-window))
    (split-window-below)))

(defun eipb2--suite ()
  ;; --- signal cells: mid-buffer insert + visible ---
  ;; NOTE: fully deterministic content -- no random units.
  (dolist (spec '((100000 "mb/100KB") (1040000 "mb/1MB")))
    (let* ((bytes (nth 0 spec))
           (tag   (nth 1 spec))
           (buf   (eipb2--fill-buffer
                   "*p21*" bytes "printf_value_42 ")))
      (goto-char (/ (+ (point-min) (point-max)) 2))
      (redisplay t)
      (eipb2--cell tag 150
        (insert "x")
        (redisplay t))
      (kill-buffer buf)))
  ;; --- control: EOB insert + visible (phase-2 showed D==A here) ---
  (let ((buf (eipb2--fill-buffer "*p21*" 1040000 "printf_value_7 ")))
    (goto-char (point-max))
    (redisplay t)
    (eipb2--cell "eob/1MB" 150
      (insert "x")
      (redisplay t))
    (kill-buffer buf))
  ;; --- control: idle redisplay ---
  (let ((buf (eipb2--fill-buffer "*p21*" 1040000 "printf_value_7 ")))
    (goto-char (/ (+ (point-min) (point-max)) 2))
    (redisplay t)
    (eipb2--cell "noop/1MB" 150
      (redisplay t))
    (kill-buffer buf))
  ;; --- 8 windows x mid-buffer editvis ---
  (let ((buf (eipb2--fill-buffer "*p21*" 1040000 "printf_value_7 ")))
    (goto-char (/ (+ (point-min) (point-max)) 2))
    (eipb2--set-windows 8)
    (redisplay t)
    (eipb2--cell "win8/mb" 60
      (insert "x")
      (redisplay t))
    (delete-other-windows)
    (kill-buffer buf))
  ;; --- 100-buffer switch cycle ---
  (let ((bufs nil))
    (dotimes (i 100)
      (let ((b (get-buffer-create (format "*p21-%03d*" i))))
        (with-current-buffer b
          (setq buffer-undo-list t)
          (erase-buffer)
          (dotimes (_ 8000) (insert "beta_symbol "))
          (goto-char (/ (point-max) 2)))
        (push b bufs)))
    (setq bufs (nreverse bufs))
    (set-window-buffer (selected-window) (nth 0 bufs))
    (redisplay t)
    ;; deterministic cyclic trace -- identical sequence every build/round
    (let ((idx 0))
      (eipb2--cell "bufswitch" 200
        (setq idx (% (1+ idx) 100))
        (switch-to-buffer (nth idx bufs))
        (redisplay t)))
    (dolist (b bufs) (kill-buffer b))))

(message "=== EIPB P21 BEGIN (%s) ===" eipb2--build)
(ignore-errors (set-frame-height (selected-frame) 50))
(ignore-errors (set-frame-width  (selected-frame) 170))
(condition-case err
    (progn (eipb2--suite) 'OK)
  (error
   (eipb2--append-line
    (format "EIPB2|%s|FATAL|msg|ERROR: %S\n" eipb2--build err))))
(kill-emacs 0)
