;;; eipb_p33_core.el --- EIPB Phase 3.3 dired coverage -*- lexical-binding: t; -*-
;;
;; Minimal frozen set (T3 tail): empty/small/big dir opens,
;; refresh, create/rename/delete (+revert), dired<->file jump.
;; COVERAGE phase -- observed differences only (doctrine 8).
;;
;; Cells (each op includes forced redisplay => command->visible):
;;   dd/open-empty x3      dd/open-small x3     dd/open-big x3
;;   dd/refresh-small x5   dd/create x5  dd/rename x5  dd/delete x5
;;   dd/jump x5            (dired -> file -> back)
;;
;; Output: EIPB3|<build>|<cell>|<metric>|<value>

(require 'cl-lib)
(require 'dired)

(defconst eipb3--build (or (getenv "EIPB_BUILD") "UNKNOWN"))

(defun eipb3--log ()
  (or (getenv "EIPB_LOG") "/tmp/eipb_t33_out.txt"))

(defun eipb3--append-line (line)
  (with-temp-buffer
    (insert line)
    (append-to-file (point-min) (point-max) (eipb3--log))))

(defun eipb3--emit (cell metric value)
  (eipb3--append-line
   (format "EIPB3|%s|%s|%s|%.4f\n" eipb3--build cell metric value)))

(defun eipb3--note (cell text)
  (eipb3--append-line
   (format "EIPB3|%s|%s|msg|%s\n" eipb3--build cell text)))

(defun eipb3--pct (l p)
  (let ((s (sort (copy-sequence l) #'<)) (n (length l)))
    (nth (max 0 (min (1- n) (1- (ceiling (* (/ p 100.0) n))))) s)))

(defmacro eipb3--op (act &rest body)
  (declare (indent 1))
  `(let ((t0 (float-time)))
     ,@body
     (redisplay t)
     (undo-boundary)
     (eipb3--emit ,act "d_ms" (* (- (float-time) t0) 1000.0))))

(defun eipb3--kill-all-buffers-silently ()
  (switch-to-buffer (get-buffer-create "*scratch*"))
  (let (kill-buffer-query-functions)
    (dolist (b (buffer-list))
      (unless (eq b (current-buffer))
        (set-buffer-modified-p nil)
        (ignore-errors (kill-buffer b))))))

;; ---------------- fixture ----------------

(defun eipb3--make-dirs (root)
  "Create empty/, small/(20), big/(2000).  Returns alist."
  (let ((mk (lambda (name n)
              (let ((d (file-name-as-directory
                        (expand-file-name name root))))
                (make-directory d t)
                (dotimes (i n)
                  (with-temp-file
                      (expand-file-name
                       (format "f-%04d.txt" i) d)
                    (insert "payload\n")))
                (cons name d)))))
    (list (funcall mk "empty" 0)
          (funcall mk "small" 20)
          (funcall mk "big"   2000))))

(defun eipb3--dir-of (dirs name)
  (cdr (assoc name dirs)))

;; ---------------- sections ----------------

(defun eipb3--open-cells (dirs)
  ;; fresh dired buffer per open: kill between reps to measure cold
  ;; listing each time (the user-perceived directory entry action)
  (dolist (spec '(("empty" "dd/open-empty")
                  ("small" "dd/open-small")
                  ("big"   "dd/open-big")))
    (let ((dir (eipb3--dir-of dirs (car spec)))
           (cell (nth 1 spec)))
      (dotimes (_ 3)
        (eipb3--op cell
          (dired dir))
        (kill-buffer (current-buffer))
        (undo-boundary)))))

(defun eipb3--mutate-cells (dirs)
  (let ((small (eipb3--dir-of dirs "small"))
        (dbuf nil))
    (dired small)
    (setq dbuf (current-buffer))
    ;; refresh only
    (dotimes (_ 5)
      (with-current-buffer dbuf
        (eipb3--op "dd/refresh-small"
          (revert-buffer))))
    ;; create + auto-revert chain (x5)
    (dotimes (i 5)
      (with-current-buffer dbuf
        (eipb3--op "dd/create"
          (progn
            (write-region "" nil
                          (expand-file-name (format "new-%02d.txt" i) small))
            (revert-buffer)))))
    ;; rename + revert (x5)
    (dotimes (i 5)
      (with-current-buffer dbuf
        (eipb3--op "dd/rename"
          (progn
            (rename-file (expand-file-name (format "new-%02d.txt" i) small)
                         (expand-file-name (format "ren-%02d.txt" i) small))
            (revert-buffer)))))
    ;; delete + revert (x5): remove renamed files, keep dir clean-ish
    (dotimes (i 5)
      (with-current-buffer dbuf
        (eipb3--op "dd/delete"
          (progn
            (ignore-errors
              (delete-file (expand-file-name
                            (format "ren-%02d.txt" i) small)))
            (revert-buffer)))))
    ;; cleanup leftovers
    (dotimes (i 5)
      (ignore-errors
        (delete-file (expand-file-name (format "new-%02d.txt" i) small))))
    (kill-buffer dbuf)))

(defun eipb3--jump-cell (dirs)
  (let ((small (eipb3--dir-of dirs "small"))
        (target (expand-file-name "f-0000.txt"
                                  (eipb3--dir-of dirs "small"))))
    (dired small)
    (let ((dbuf (current-buffer)))
      (dotimes (_ 5)
        (with-current-buffer dbuf
          ;; exact jump: first visible lines may be "." / ".." dirs
          (dired-goto-file target)
          (eipb3--op "dd/jump"
            (progn
              (dired-find-file)
              (switch-to-buffer dbuf)))))
      (kill-buffer dbuf))))

;; ---------------- main ----------------

(message "=== EIPB P33 BEGIN (%s) ===" eipb3--build)
(ignore-errors (set-frame-height (selected-frame) 50))
(ignore-errors (set-frame-width  (selected-frame) 170))
(condition-case err
    (let* ((root (file-name-as-directory (make-temp-file "eipb-p33" t)))
           (dirs (eipb3--make-dirs root)))
      (eipb3--open-cells dirs)
      (eipb3--mutate-cells dirs)
      (eipb3--jump-cell dirs)
      (eipb3--note "teardown" "killing-buffers")
      (eipb3--kill-all-buffers-silently)
      (eipb3--note "teardown" "deleting-tree")
      (ignore-errors (delete-directory root t))
      (eipb3--note "teardown" "done")
      (let ((confirm-kill-processes nil))
        (kill-emacs 0)))
  (error (eipb3--append-line
          (format "EIPB3|%s|FATAL|msg|P33 ERROR: %S\n" eipb3--build err))))
(kill-emacs 0)
