;;; eipb_p31_core.el --- EIPB Phase 3.1 IDE-MIXED-01 scripted session -*- lexical-binding: t; -*-
;;
;; Frozen script (EIPB.md T3): open project -> switch -> type ->
;; complete(local dabbrev stand-in) -> scroll -> search -> edit ->
;; undo -> re-complete -> window cycle -> save -> idle.
;;
;; LSP exclusion (documented cut): real language servers are out of
;; EIPB's emacs-core scope; LSP transport was measured separately in
;; EVS-4.3/EVS-5.4.  Local completion stands in via dabbrev-expand,
;; which exercises the interactive completion path without a server.
;;
;; Trace: every timed op appends one line -- log order IS the
;; chronological trace:
;;   EIPB3|<build>|mix/<action>|d_ms|<duration>
;; Phase markers + session summary follow as msg/metric lines.
;;
;; COVERAGE phase -- observed differences only (doctrine 8).

(require 'cl-lib)

(defconst eipb3--build (or (getenv "EIPB_BUILD") "UNKNOWN"))

(defun eipb3--log ()
  (or (getenv "EIPB_LOG") "/tmp/eipb_mixed_out.txt"))

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

;; ---------------- content ----------------

(defun eipb3--unit-for (kind i)
  (pcase kind
    ("c"      (format "/* u%d */\nstatic int fn_%d(int a, int b)\n{\n    return a + b + %d;\n}\n\n" i i i))
    ("elisp"  (format "(defun fn-%d (a b)\n  \"Doc %d.\"\n  (+ a b %d))\n" i i i))
    ("python" (format "def fn_%d(a, b):\n    return a + b + %d\n\n" i i))
    ("org"    (format "* H%d\nbody *bold* [[https://ex][l]] text\n- item\n\n" i))
    (_        (format "printf_value_%04d payload line %d\n" i i))))

(defvar eipb3--project-spec
  ;; kind count bytes-per-file  (deterministic small project ~250KB)
  '(("elisp"  6 30000)
    ("c"      4 20000)
    ("python" 3 15000)
    ("org"    3 15000)
    (nil      4 15000)))

(defun eipb3--ext-for (kind)
  (pcase kind ("c" "c") ("elisp" "el") ("python" "py") ("org" "org") (_ "txt")))

(defun eipb3--mode-for (kind)
  (pcase kind ("c" 'c-mode) ("elisp" 'emacs-lisp-mode)
              ("python" 'python-mode) ("org" 'org-mode) (_ 'fundamental-mode)))

(defun eipb3--make-project (dir)
  (let ((files nil) (idx 0))
    (dolist (spec eipb3--project-spec)
      (let ((kind (nth 0 spec)) (count (nth 1 spec)) (bytes (nth 2 spec)))
        (dotimes (f count)
          (let* ((base (if kind
                           (format "mod-%s-%02d.%s" kind f (eipb3--ext-for kind))
                         (format "data-%02d.txt" f)))
                 (path (expand-file-name base dir)))
            (with-temp-file path
              (while (< (buffer-size) bytes)
                (insert (eipb3--unit-for kind idx))
                (setq idx (1+ idx))))
            (push path files)))))
    (nreverse files)))

(defun eipb3--find-buffer-with-mode (buffers mode)
  (cl-find-if (lambda (b) (with-current-buffer b (derived-mode-p mode)))
              buffers))

;; ---------------- timed-op helpers ----------------

(defmacro eipb3--op (act &rest body)
  "Time BODY (+forced redisplay), append one trace line.
Appends an undo-boundary afterwards: real commands get one per
command, scripted inserts would otherwise collapse into a single
giant undo segment."
  (declare (indent 1))
  `(let ((t0 (float-time)))
     ,@body
     (redisplay t)
     (undo-boundary)
     (eipb3--emit ,act "d_ms" (* (- (float-time) t0) 1000.0))))

(defun eipb3--completion-round (cell errs)
  "One prefix+expand+scrub cycle; returns updated ERRS count.
Uses the NATIVE completion entrypoint (completion-at-point): it is
stateless across calls.  dabbrev was tried first and abandoned --
its cross-call bookkeeping (last-abbreviation/expansion/table)
breaks under scripted scrub cycles with raw search-failed /
wrong-type-argument signals (smoke evidence, two iterations).
elisp capf collects identifier-like tokens from buffer text, so
the project's fn-NNNN definitions are natural candidates."
  (let ((beg (point)) (ok t) (t0 (float-time)) (emsg nil))
    (insert "fn-")
    (condition-case e
        (completion-at-point)
      (error (setq ok nil emsg (format "%S" e))))
    (redisplay t)
    (eipb3--emit cell "d_ms" (* (- (float-time) t0) 1000.0))
    (when (not ok)
      (eipb3--note cell (format "capf_error %s" emsg)))
    ;; capf may pop a *Completions* window for ambiguous prefixes --
    ;; closing it is part of the user-visible flow, do it untimed.
    (let ((cw (get-buffer-window "*Completions*")))
      (when cw (delete-window cw)))
    (delete-region beg (point))
    (undo-boundary)
    (if ok errs (1+ errs))))

;; ---------------- IDE-MIXED-01 session ----------------

(defun eipb3--mixed-session ()
  (let* ((dir (file-name-as-directory (make-temp-file "eipb-mix" t)))
         (files (eipb3--make-project dir))
         (t-session (float-time))
         (ops 0)
         (buffers nil))
    (setq make-backup-files nil)   ; controlled fs surface, symmetric
    ;; ---- phase: open project ----
    (eipb3--note "mix/phase" "open")
    (dolist (f files)
      (eipb3--op "mix/open"
        (find-file f))
      (push (current-buffer) buffers)
      (cl-incf ops))
    (setq buffers (nreverse buffers))
    ;; ---- phase: switch cycle ----
    (eipb3--note "mix/phase" "switch")
    (dotimes (i 20)
      (eipb3--op "mix/switchbuf"
        (switch-to-buffer (nth (% (1+ i) (length buffers)) buffers)))
      (cl-incf ops))
    ;; ---- phase: typing burst (c-mode file: jit engaged) ----
    (eipb3--note "mix/phase" "typing-c")
    (let ((cb (eipb3--find-buffer-with-mode buffers 'c-mode)))
      (switch-to-buffer cb)
      (goto-char (/ (+ (point-min) (point-max)) 2))
      (redisplay t)
      (dotimes (_ 50)
        (eipb3--op "mix/typevis"
          (insert "x"))
        (cl-incf ops)))
    ;; ---- phase: completion (local dabbrev stand-in) ----
    (eipb3--note "mix/phase" "complete")
    (let ((eb (eipb3--find-buffer-with-mode buffers 'emacs-lisp-mode))
          (errs 0))
      (switch-to-buffer eb)
      (goto-char (/ (+ (point-min) (point-max)) 2))
      (redisplay t)
      (dotimes (_ 10)
        (setq errs (eipb3--completion-round "mix/capf" errs))
        (cl-incf ops))
      (when (> errs 0)
        (eipb3--emit "mix/capf" "errors" (float errs))))
    ;; ---- phase: scroll ----
    (eipb3--note "mix/phase" "scroll")
    (dotimes (i 30)
      (eipb3--op "mix/scroll"
        (if (= 0 (% i 2)) (scroll-up-line) (scroll-down-line)))
      (cl-incf ops))
    ;; ---- phase: search burst ----
    (eipb3--note "mix/phase" "isearch")
    (isearch-mode t)
    (dolist (ch (string-to-list "fn_42"))
      (eipb3--op "mix/isearchkey"
        (isearch-process-search-char ch))
      (cl-incf ops))
    (isearch-exit)
    ;; ---- phase: edit + delete pairs ----
    (eipb3--note "mix/phase" "editdel")
    (dotimes (_ 20)
      (eipb3--op "mix/editdel"
        (insert "tmp_edit_token ")
        (delete-char -16))
      (cl-incf ops))
    ;; ---- phase: undo steps (T1 discipline: primitive-undo + setq).
    ;; eipb3--op appends undo-boundary per op, so each primitive-undo
    ;; consumes exactly one scripted command-step, like a real user.
    (eipb3--note "mix/phase" "undo")
    (let ((i 0))
      (while (and (< i 20) buffer-undo-list)
        (eipb3--op "mix/undostep"
          (setq buffer-undo-list (primitive-undo 1 buffer-undo-list)))
        (cl-incf i)
        (cl-incf ops)))
    ;; ---- phase: re-complete AFTER edits (contract order) ----
    (eipb3--note "mix/phase" "recomplete")
    (let ((errs 0))
      (dotimes (_ 5)
        (setq errs (eipb3--completion-round "mix/capf2" errs))
        (cl-incf ops))
      (when (> errs 0)
        (eipb3--emit "mix/capf2" "errors" (float errs))))
    ;; ---- phase: window cycle ----
    (eipb3--note "mix/phase" "windows")
    (split-window-below)
    (other-window 1)
    (dotimes (_ 8)
      (eipb3--op "mix/wincycle"
        (other-window 1)
        (insert "w"))
      (cl-incf ops))
    (delete-other-windows)
    ;; ---- phase: save ----
    (eipb3--note "mix/phase" "save")
    (dolist (b (list (nth 0 buffers) (nth 1 buffers) (nth 2 buffers)))
      (switch-to-buffer b)
      (goto-char (point-max))
      (insert ";\nsave-touch\n")
      (eipb3--op "mix/save"
        (basic-save-buffer))
      (cl-incf ops))
    ;; ---- final idle drain ----
    (eipb3--op "mix/idle" (sit-for 0))
    (cl-incf ops)
    ;; ---- session summary ----
    (eipb3--emit "mix/session" "wall_ms" (* (- (float-time) t-session) 1000.0))
    (eipb3--emit "mix/session" "actions" (float ops))
    ;; kill-buffer PROMPTS on modified file-visiting buffers and hangs
    ;; the scripted session forever; clear modified flags + queries.
    (dolist (b buffers)
      (with-current-buffer b
        (set-buffer-modified-p nil)
        (unlock-buffer)))
    (let (kill-buffer-query-functions)
      (mapc (lambda (b) (ignore-errors (kill-buffer b))) buffers))
    (ignore-errors (delete-directory dir t))))

(message "=== EIPB P31 MIXED BEGIN (%s) ===" eipb3--build)
(ignore-errors (set-frame-height (selected-frame) 50))
(ignore-errors (set-frame-width  (selected-frame) 170))
(condition-case err
    (progn (eipb3--mixed-session) 'OK)
  (error (eipb3--append-line
          (format "EIPB3|%s|FATAL|msg|MIX ERROR: %S\n" eipb3--build err))))
(kill-emacs 0)
