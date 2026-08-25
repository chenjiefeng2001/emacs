;;; eipb_p32_core.el --- EIPB Phase 3.2 xref/imenu + org coverage -*- lexical-binding: t; -*-
;;
;; T3 remainder, priority order per plan: (1) xref/imenu IDE symbol
;; paths, (2) org content-system workload.  COVERAGE phase --
;; observed differences only (doctrine 8); no optimization claims.
;;
;; Cells:
;;   im/elisp/index  imenu index build on elisp buffer   x5
;;   im/goto         jump-to-entry + visible             x5
;;   im/c/index      imenu index build on c buffer       x5
;;   xr/scan         xref-matches-in-directory (grep)    x3
;;   org/fontify     cold whole-org fontify segments     x1
;;   org/cycle       subtree fold/unfold + visible       x10
;;   org/global      whole-buffer visibility sweep       x4
;;   org/nav         outline-next-heading + visible      x10
;;
;; Output: EIPB3|<build>|<cell>|<metric>|<value>

(require 'cl-lib)
(require 'imenu)
(require 'xref)
(require 'org)

(defconst eipb3--build (or (getenv "EIPB_BUILD") "UNKNOWN"))

(defun eipb3--log ()
  (or (getenv "EIPB_LOG") "/tmp/eipb_t32_out.txt"))

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

;; ---------------- content generators ----------------

(defun eipb3--unit-for (kind i)
  (pcase kind
    ("c"      (format "/* u%d */\nstatic int fn_%d(int a, int b)\n{\n    return a + b + %d;\n}\n\n" i i i))
    ("elisp"  (format "(defun fn-%d (a b)\n  \"Doc %d.\"\n  (+ a b %d))\n" i i i))
    (_        (format "printf_value_%04d payload line %d\n" i i))))

(defun eipb3--org-heading (i)
  (format "* Heading %04d\nBody text with *bold* and [[https://ex][link]].\n:LOGBOOK:\nCLOCK: [2026-01-01 Thu 10:%02d]\n:END:\n- item one\n- item two\n\n"
          i (% i 60)))

(defvar eipb3--project-spec
  '(("elisp" 6 30000) ("c" 4 20000) (nil 4 15000)))

(defun eipb3--make-project (dir)
  (let ((files nil) (idx 0))
    (dolist (spec eipb3--project-spec)
      (let ((kind (nth 0 spec))
            (count (nth 1 spec))
            (bytes (nth 2 spec)))
        (dotimes (f count)
          (let* ((ext (pcase kind ("c" "c") ("elisp" "el") (_ "txt")))
                 (base (if kind
                           (format "mod-%s-%02d.%s" kind f ext)
                         (format "data-%02d.txt" f)))
                 (path (expand-file-name base dir)))
            (with-temp-file path
              (while (< (buffer-size) bytes)
                (insert (eipb3--unit-for kind idx))
                (setq idx (1+ idx))))
            (push path files)))))
    (nreverse files)))

(defun eipb3--make-org-file (path headings)
  (with-temp-file path
    (dotimes (i headings)
      (insert (eipb3--org-heading i)))))

;; ---------------- op helper ----------------

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

;; ---------------- IMENU / XREF ----------------

(defun eipb3--first-pos (x)
  "First numeric imenu position anywhere inside alist tree X."
  (cond ((and (consp x) (numberp (cdr x))) (cdr x))
        ((consp x) (or (eipb3--first-pos (car x))
                       (eipb3--first-pos (cdr x))))
        (t nil)))

(defun eipb3--imenu-elisp (path)
  (find-file path)
  (let ((entries nil) (pos nil))
    (dotimes (_ 5)
      (eipb3--op "im/elisp/index"
        (setq entries (imenu--make-index-alist))))
    (setq pos (eipb3--first-pos entries))
    (if pos
        (progn
          (goto-char pos)
          (redisplay t)
          (dotimes (_ 5)
            (eipb3--op "im/goto"
              (goto-char pos))))
      (eipb3--note "im/goto" "no_numeric_entry_found"))))

(defun eipb3--imenu-c (path)
  (find-file path)
  (dotimes (_ 5)
    (eipb3--op "im/c/index"
      (imenu--make-index-alist))))

(defun eipb3--xref-scan (dir)
  ;; FILES is find-glob semantics (space-separated): "*" matches all;
  ;; ".*" would match DOTFILES ONLY (smoke evidence: 0 hits).
  (let ((cnt nil))
    (dotimes (_ 3)
      (let ((res nil))
        (eipb3--op "xr/scan"
          (setq res (xref-matches-in-directory
                     "fn_[0-9]+" "*" dir nil)))
        (push (float (length res)) cnt)))
    (when cnt
      (eipb3--emit "xr/scan" "matches_mean"
                   (/ (apply '+ cnt) (float (length cnt)))))))

;; ---------------- ORG ----------------

(defun eipb3--org-open-fontify (path)
  (find-file path)
  (org-mode)
  (let* ((t1 (float-time)))
    (font-lock-ensure)
    (let ((fo (* (- (float-time) t1) 1000.0)))
      (eipb3--emit "org/fontify" "font_ms" fo))))

(defun eipb3--org-interact ()
  ;; park under a heading mid-buffer
  (goto-char (/ (+ (point-min) (point-max)) 4))
  (outline-next-heading)
  (redisplay t)
  (dotimes (_ 10)
    (eipb3--op "org/cycle"
      (org-cycle)))
  ;; whole-buffer visibility sweeps, toggled back and forth
  (dotimes (_ 2)
    (eipb3--op "org/global"
      (org-global-cycle))
    (eipb3--op "org/global"
      (org-global-cycle)))
  (dotimes (_ 10)
    (eipb3--op "org/nav"
      (outline-next-heading))))

;; ---------------- main ----------------

(message "=== EIPB P32 BEGIN (%s) ===" eipb3--build)
(ignore-errors (set-frame-height (selected-frame) 50))
(ignore-errors (set-frame-width  (selected-frame) 170))
(condition-case err
    (let ((dir (file-name-as-directory (make-temp-file "eipb-p32" t))))
      (eipb3--make-project dir)
      ;; --- imenu/xref section ---
      (eipb3--imenu-elisp (expand-file-name "mod-elisp-00.el" dir))
      (eipb3--imenu-c     (expand-file-name "mod-c-00.c" dir))
      (eipb3--kill-all-buffers-silently)
      (eipb3--xref-scan dir)
      ;; --- org section ---
      (let ((opath (expand-file-name "notes.org" dir)))
        (eipb3--make-org-file opath 2000)
        (find-file opath)
        (org-mode)
        (eipb3--org-open-fontify opath)
        (eipb3--org-interact))
      ;; teardown probes: locate any stall precisely
      (eipb3--note "teardown" "killing-buffers")
      (eipb3--kill-all-buffers-silently)
      (eipb3--note "teardown" "deleting-dir")
      (ignore-errors (delete-directory dir t))
      (eipb3--note "teardown" "done")
      ;; kill-emacs can PROMPT on processes with query-on-exit set
      ;; (observed: hang after all teardown notes).  Log them, then
      ;; disable the confirmation.
      (dolist (p (process-list))
        (eipb3--note "teardown"
                     (format "live-process %s status=%s"
                             (process-name p) (process-status p))))
      (let ((confirm-kill-processes nil))
        (kill-emacs 0)))
  (error (eipb3--append-line
          (format "EIPB3|%s|FATAL|msg|P32 ERROR: %S\n" eipb3--build err))))
(kill-emacs 0)
