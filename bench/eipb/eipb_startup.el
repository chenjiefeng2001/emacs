;;; eipb_startup.el --- EIPB tty startup milestone probe.  -*- lexical-binding: t; -*-

;; Loaded via -l into a tty emacs; records in-process milestones
;; relative to load time, then exits.  Shell side measures the total
;; wall (process launch -> exit) as cold/warm runs.

(defvar eipb--t0 (float-time))

(let* ((build (or (getenv "EIPB_BUILD") "?"))
       (log (or (getenv "EIPB_LOG") "/tmp/eipb_out.txt"))
       (tb (progn
             (get-buffer-create "*startup*")
             (- (float-time) eipb--t0))))
  (switch-to-buffer "*startup*")
  (insert "ready")
  (redisplay t)
  (let ((tr (- (float-time) eipb--t0)))
    (with-temp-buffer
      (insert (format "EIPB|%s|startup/tty|first_buffer_ms|%.4f\n"
                      build (* tb 1000.0))
              (format "EIPB|%s|startup/tty|first_redisplay_ms|%.4f\n"
                      build (* tr 1000.0)))
      (append-to-file (point-min) (point-max) log))))
(kill-emacs 0)
