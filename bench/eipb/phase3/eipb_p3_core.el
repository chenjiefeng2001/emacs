;;; eipb_p3_core.el --- EIPB Phase 3 user-path coverage -*- lexical-binding: t; -*-
;;
;; T3-A: file open chain  open -> decode -> mode -> fontify ->
;;                        redisplay -> idle   (segments timed)
;; T3-B: interactive isearch keypress -> search -> highlight ->
;;                        redisplay           (per-key dist)
;;
;; COVERAGE phase, not optimization: no causal claims, observed
;; differences only (EIPB.md doctrine 8).  GC runs NATURAL here on
;; purpose -- GC pressure is part of the user path being measured;
;; gcs-done delta emitted per cell for context.
;;
;; Output: EIPB3|<build>|<cell>|<metric>|<value>

(require 'cl-lib)

(defconst eipb3--build (or (getenv "EIPB_BUILD") "UNKNOWN"))

(defun eipb3--log ()
  (or (getenv "EIPB_LOG") "/tmp/eipb_t3_out.txt"))

(defun eipb3--append-line (line)
  (with-temp-buffer
    (insert line)
    (append-to-file (point-min) (point-max) (eipb3--log))))

(defun eipb3--emit (cell metric value)
  (eipb3--append-line
   (format "EIPB3|%s|%s|%s|%.4f\n" eipb3--build cell metric value)))

(defun eipb3--pct (l p)
  (let ((s (sort (copy-sequence l) #'<)) (n (length l)))
    (nth (max 0 (min (1- n) (1- (ceiling (* (/ p 100.0) n))))) s)))

(defun eipb3--dist (cell l)
  (setq l (cl-loop for x in l when (numberp x) collect x))
  (when l
    (eipb3--emit cell "n" (float (length l)))
    (dolist (p '(50 95 99))
      (eipb3--emit cell (format "p%d" p) (eipb3--pct l p)))
    (eipb3--emit cell "max" (car (last (sort (copy-sequence l) #'<))))))

;; ---------------- content ----------------

(defun eipb3--plain-line (i)
  (format "the quick brown fox jumps over the lazy dog 0123456789 printf_value_%04d\n" i))

(defun eipb3--elisp-unit (i)
  (format "(defun fn-%d (a b)\n  \"Doc %d.\"\n  (+ a b %d))\n\n" i i i))

(defun eipb3--make-file (path kind bytes)
  (with-temp-file path
    (let ((i 0))
      (while (< (buffer-size) bytes)
        (insert (if (eq kind 'elisp)
                    (eipb3--elisp-unit i)
                  (eipb3--plain-line i)))
        (setq i (1+ i))))))

(defun eipb3--fill-buffer (name kind bytes)
  (switch-to-buffer (get-buffer-create name))
  (setq buffer-undo-list t)
  (fundamental-mode)
  (erase-buffer)
  (let ((i 0))
    (while (< (- (point-max) (point-min)) bytes)
      (insert (if (eq kind 'elisp)
                  (eipb3--elisp-unit i)
                (eipb3--plain-line i)))
      (setq i (1+ i))))
  (goto-char (point-min))
  (current-buffer))

;; ---------------- T3-A: open chain ----------------

(defvar eipb3--file-specs
  ;; kind size tag reps fontify-p
  '((fund 100000  "f/fund/100KB") (fund 1040000 "f/fund/1MB")
    (fund 10400000 "f/fund/10MB")
    ;; runtime cap: elisp arm capped at 1MB (10MB elisp cold full
    ;; fontify ~60-70s/rep would blow the session budget x12 sessions)
    (elisp 100000 "f/elisp/100KB") (elisp 1040000 "f/elisp/1MB")))

(defun eipb3--open-cell (tag path mode-sym fontify-p reps)
  (let ((g0 gcs-done))
    (dotimes (r reps)
      (let ((buf (get-buffer-create "*p3-open*")))
        (with-current-buffer buf
          (setq buffer-undo-list t)
          (fundamental-mode)
          (erase-buffer))
        (switch-to-buffer buf)
        (let* ((t1 (float-time)))
          (insert-file-contents path t)   ; replace => fresh read+decode
          (let* ((io  (* (- (float-time) t1) 1000.0))
                 (t2 (float-time)))
            (funcall mode-sym)
            (when fontify-p (font-lock-mode 1))
            (let* ((mo  (* (- (float-time) t2) 1000.0))
                   (t3  (float-time)))
              (when fontify-p (font-lock-ensure))
              (let* ((fo  (* (- (float-time) t3) 1000.0))
                     (t4  (float-time)))
                (redisplay t)
                (let* ((rd  (* (- (float-time) t4) 1000.0))
                       (t5  (float-time)))
                  (sit-for 0)
                  (let ((idl (* (- (float-time) t5) 1000.0)))
                    (mapc
                     (lambda (pr)
                       (eipb3--emit tag (format "r%d_%s" (1+ r) (car pr))
                                    (cdr pr)))
                     `((io_ms . ,io) (mode_ms . ,mo) (font_ms . ,fo)
                       (red_ms . ,rd) (idle_ms . ,idl)))))))
            ))
        (kill-buffer buf)))
    (eipb3--emit tag "gcs_delta"
                 (float (max 0 (- (or gcs-done 0) g0))))))

(defun eipb3--file-section (dir)
  (dolist (spec eipb3--file-specs)
    (let* ((kind (nth 0 spec))
           (bytes (nth 1 spec))
           (tag (nth 2 spec))
           (size-name (cond ((>= bytes 10000000) "10MB")
                            ((>= bytes 1000000) "1MB")
                            (t "100KB")))
           (path (expand-file-name
                  (format "p3-%s-%s.txt" kind size-name) dir)))
      (eipb3--make-file path kind bytes)
      (if (eq kind 'fund)
          (eipb3--open-cell tag path 'fundamental-mode nil 2)
        (eipb3--open-cell tag path 'emacs-lisp-mode t 2)))))

;; ---------------- T3-B: interactive isearch ----------------

(defun eipb3--isearch-cell (tag bytes query)
  (let ((buf (eipb3--fill-buffer "*p3-i*" 'elisp bytes))
        (lats nil) (k 0))
    (redisplay t)
    (isearch-mode t)                       ; forward, literal
    (dolist (ch (string-to-list query))
      (setq k (1+ k))
      (let ((t0 (float-time)))
        (isearch-process-search-char ch)
        (redisplay t)                      ; includes lazy-highlight render
        (push (* (- (float-time) t0) 1000.0) lats)
        (when (= k 1)
          (eipb3--emit tag "key1_ms" (* (- (float-time) t0) 1000.0)))))
    (isearch-exit)
    (eipb3--dist tag lats)                 ; all keys incl. cold key1
    (kill-buffer buf)))

(defun eipb3--isearch-section ()
  (dolist (spec '((100000 "i/100KB") (1040000 "i/1MB")))
    (let ((bytes (nth 0 spec)) (pre (nth 1 spec)))
      (eipb3--isearch-cell (concat pre "/hit")  bytes "printf_value_42")
      (eipb3--isearch-cell (concat pre "/miss") bytes "zzq_nope_777"))))

;; ---------------- main ----------------

(message "=== EIPB P3 BEGIN (%s) ===" eipb3--build)
(condition-case err
    (let ((dir (file-name-as-directory (make-temp-file "eipb-p3" t))))
      (eipb3--file-section dir)
      (eipb3--isearch-section)
      (ignore-errors
        (delete-directory dir t)))
  (error (eipb3--append-line
          (format "EIPB3|%s|FATAL|msg|ERROR: %S\n" eipb3--build err))))
(kill-emacs 0)
