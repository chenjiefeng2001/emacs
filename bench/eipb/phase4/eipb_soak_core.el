;;; eipb_soak_core.el --- EIPB Phase 4.1 SOAK mixed-load drift -*- lexical-binding: t; -*-
;;
;; SOAK-30M (default): replay atlas paths continuously; measure
;; LONG-RUN STATE DRIFT.  Observation layer only.
;;
;; Windows (default 10 x 180 s): per-window op percentiles
;; (overall AND per class -- drift series for closed/stall row
;; stability), gcs-delta, memory-limit.  Whole-run per-class
;; distributions + TAIL AMPLIFICATION = p99(last 10 min) /
;; p99(first 10 min), wall-clock buckets, overall and per major
;; class -- HARD Go/No-Go gate (EIPB.md T4 frozen definition).
;;
;; ENCA internal counters (scheduler/snapshot/cache): NOT
;; elisp-observable in current builds => PENDING-API, excluded
;; from gates (doctrine: recorded, never guessed).
;;
;; Output: EIPB4|<build>|<cell>|<metric>|<value>

(require 'cl-lib)
(require 'imenu)
(require 'xref)
(require 'org)

(defconst eipb4--build (or (getenv "EIPB_BUILD") "UNKNOWN"))
(defconst eipb4--secs  (string-to-number (or (getenv "EIPB_SOAK_SECS") "1800")))
(defconst eipb4--win-s (string-to-number (or (getenv "EIPB_SOAK_WIN") "180")))

(defun eipb4--log ()
  (or (getenv "EIPB_LOG") "/tmp/eipb_t41_out.txt"))

(defun eipb4--append-line (line)
  (with-temp-buffer
    (insert line)
    (append-to-file (point-min) (point-max) (eipb4--log))))

(defun eipb4--emit (cell metric value)
  (eipb4--append-line
   (format "EIPB4|%s|%s|%s|%.4f\n" eipb4--build cell metric value)))

(defun eipb4--note (cell text)
  (eipb4--append-line
   (format "EIPB4|%s|%s|msg|%s\n" eipb4--build cell text)))

(defun eipb4--pct (l p)
  (let ((s (sort (copy-sequence l) #'<)) (n (length l)))
    (nth (max 0 (min (1- n) (1- (ceiling (* (/ p 100.0) n))))) s)))

;; ---------------- content ----------------

(defun eipb4--unit-for (kind i)
  (pcase kind
    ("c"      (format "/* u%d */\nstatic int fn_%d(int a, int b)\n{\n    return a + b + %d;\n}\n\n" i i i))
    ("elisp"  (format "(defun fn-%d (a b)\n  \"Doc %d.\"\n  (+ a b %d))\n" i i i))
    ("python" (format "def fn_%d(a, b):\n    return a + b + %d\n\n" i i))
    ("org"    (format "* H%d\nbody *bold* [[https://ex][l]]\n- item\n\n" i))
    (_        (format "printf_value_%04d payload line %d\n" i i))))

(defvar eipb4--project-spec
  '(("elisp" 6 30000) ("c" 4 20000) ("python" 3 15000)
    ("org" 3 15000) (nil 4 15000)))

(defun eipb4--make-project (dir)
  (let ((files nil) (idx 0))
    (dolist (spec eipb4--project-spec)
      (let ((kind (nth 0 spec)) (count (nth 1 spec)) (bytes (nth 2 spec)))
        (dotimes (f count)
          (let* ((ext (pcase kind ("c" "c") ("elisp" "el")
                              ("python" "py") ("org" "org") (_ "txt")))
                 (path (expand-file-name
                        (if kind (format "mod-%s-%02d.%s" kind f ext)
                          (format "data-%02d.txt" f))
                        dir)))
            (with-temp-file path
              (while (< (buffer-size) bytes)
                (insert (eipb4--unit-for kind idx))
                (setq idx (1+ idx))))
            (push path files)))))
    (nreverse files)))

(defun eipb4--find-mode-buffer (buffers mode)
  (cl-find-if (lambda (b) (with-current-buffer b (derived-mode-p mode)))
              buffers))

;; ---------------- observation state ----------------

(defvar eipb4--buffers nil)
(defvar eipb4--dir nil)
(defvar eipb4--win-ms nil)     ; this window's op durations
(defvar eipb4--win-cls nil)    ; hash: class -> this window's ms
(defvar eipb4--all-cls nil)    ; hash: class -> whole-run ms
(defvar eipb4--head-all nil)   ; ms during first wall-clock bucket
(defvar eipb4--tail-all nil)   ; ms during last wall-clock bucket
(defvar eipb4--head-cls nil)   ; hash: class -> head-bucket ms
(defvar eipb4--tail-cls nil)   ; hash: class -> tail-bucket ms
(defvar eipb4--t0 0.0)         ; load-loop wall-clock origin
(defvar eipb4--bucket 600.0)   ; contract: first/last 10 minutes
(defvar eipb4--ops 0)
(defvar eipb4--wins-done 0)
(defvar eipb4--t-cycle-start 0.0)

(defun eipb4--record (class ms)
  (push ms eipb4--win-ms)
  (push ms (gethash class eipb4--win-cls))
  (push ms (gethash class eipb4--all-cls))
  (let ((dt (- (float-time) eipb4--t0)))
    (when (< dt eipb4--bucket)
      (push ms eipb4--head-all)
      (push ms (gethash class eipb4--head-cls)))
    (when (>= dt (- eipb4--secs eipb4--bucket))
      (push ms eipb4--tail-all)
      (push ms (gethash class eipb4--tail-cls))))
  (cl-incf eipb4--ops))

(defun eipb4--op (class fn)
  (let* ((t0 (float-time)))
    (funcall fn)
    (redisplay t)
    (undo-boundary)
    (eipb4--record class (* (- (float-time) t0) 1000.0))))

(defun eipb4--window-close (widx g0 m0)
  (when eipb4--win-ms
    (setq eipb4--wins-done (1+ eipb4--wins-done))
    (eipb4--emit (format "win%02d" widx) "n"
                 (float (length eipb4--win-ms)))
    (dolist (p '(50 95 99))
      (eipb4--emit (format "win%02d" widx) (format "p%d" p)
                   (eipb4--pct eipb4--win-ms p)))
    (eipb4--emit (format "win%02d" widx) "max"
                 (car (last (sort (copy-sequence eipb4--win-ms) #'<))))
    (eipb4--emit (format "win%02d" widx) "gcs_delta"
                 (float (max 0 (- (or gcs-done 0) (or g0 0)))))
    (eipb4--emit (format "win%02d" widx) "memlimit_kb" (float (or m0 0)))
    ;; per-class drift series (closed/stall row stability over time)
    (maphash
     (lambda (class ms)
       (let ((cell (format "win%02d/%s" widx class)))
         (dolist (p '(50 99))
           (eipb4--emit cell (format "p%d" p) (eipb4--pct ms p)))
         (eipb4--emit cell "max"
                      (car (last (sort (copy-sequence ms) #'<))))))
     eipb4--win-cls))
  (setq eipb4--win-ms nil
        eipb4--win-cls (make-hash-table :test 'equal)))

(defun eipb4--ta-one (cell head tail)
  "Emit TA = p99(tail)/p99(head) for one series pair.  HARD GATE."
  (when (and (> (length head) 24) (> (length tail) 24))
    (let* ((ph (eipb4--pct head 99))
           (pt (eipb4--pct tail 99)))
      (when (> ph 0)
        (eipb4--emit cell "ta_ratio" (/ pt ph))
        (eipb4--emit cell "head_p99" ph)
        (eipb4--emit cell "tail_p99" pt)
        (eipb4--emit cell "head_n" (float (length head)))
        (eipb4--emit cell "tail_n" (float (length tail)))))))

(defun eipb4--emit-tail-amplification ()
  "TA = p99(last 10 min)/p99(first 10 min), overall + per class.
Frozen T4 metric definition (EIPB.md section T4).  HARD GATE."
  (eipb4--ta-one "ta/overall" eipb4--head-all eipb4--tail-all)
  (maphash
   (lambda (class _ignored)
     (eipb4--ta-one (concat "ta/" class)
                    (gethash class eipb4--head-cls)
                    (gethash class eipb4--tail-cls)))
   eipb4--all-cls))

;; ---------------- workload ----------------

(defun eipb4--cycle (cyc)
  (let ((cb (eipb4--find-mode-buffer eipb4--buffers 'c-mode))
        (eb (eipb4--find-mode-buffer eipb4--buffers 'emacs-lisp-mode))
        (ob (eipb4--find-mode-buffer eipb4--buffers 'org-mode)))
    ;; typing bursts (jit engaged)
    (switch-to-buffer cb)
    (goto-char (/ (+ (point-min) (point-max)) 2))
    (dotimes (_ 10)
      (eipb4--op "type-c" (lambda () (insert "x"))))
    (switch-to-buffer eb)
    (goto-char (/ (+ (point-min) (point-max)) 2))
    (dotimes (_ 10)
      (eipb4--op "type-el" (lambda () (insert "y"))))
    ;; completion round (native capf, self-scrubbing)
    (goto-char (- (point-max) 500))
    (let ((beg (point)))
      (insert "fn-")
      (eipb4--op "capf"
        (lambda ()
          (condition-case nil (completion-at-point) (error nil))))
      (let ((cw (get-buffer-window "*Completions*")))
        (when cw (delete-window cw)))
      (delete-region beg (point))
      (undo-boundary))
    ;; imenu refresh after edits
    (with-current-buffer eb
      (eipb4--op "imenu"
        (lambda () (ignore-errors (imenu--make-index-alist)))))
    ;; xref scan (real grep subprocess)
    (eipb4--op "xref"
      (lambda ()
        (ignore-errors
          (xref-matches-in-directory "fn_[0-9]+" "*" eipb4--dir nil))))
    ;; dired fresh small-dir listing
    (eipb4--op "dired"
      (lambda ()
        (let ((d (file-name-as-directory
                  (expand-file-name "small" eipb4--dir))))
          (unless (file-directory-p d)
            (make-directory d t)
            (dotimes (i 20)
              (write-region "" nil
                            (expand-file-name
                             (format "f-%02d.txt" i) d))))
          (let ((buf (dired d)))
            (with-current-buffer buf (revert-buffer))
            (kill-buffer buf)))))
    ;; org subtree cycles (+periodic global sweep)
    (when ob
      (switch-to-buffer ob)
      (goto-char (/ (+ (point-min) (point-max)) 4))
      (outline-next-heading)
      (dotimes (_ 2)
        (eipb4--op "orgcycle"
          (lambda () (condition-case nil (org-cycle) (error nil)))))
      (when (= (% cyc 25) 0)
        (dotimes (_ 2)
          (eipb4--op "orgglobal"
            (lambda ()
              (condition-case nil (org-global-cycle) (error nil)))))))
    ;; window edit cycling
    (dotimes (_ 4)
      (eipb4--op "winedit"
        (lambda ()
          (other-window 1)
          (insert "w"))))
    ;; forced-GC pause sample (pause-drift series)
    (eipb4--op "gcpause" (lambda () (garbage-collect)))
    ;; idle pacing: force a >=4 s cycle floor.  Plain sit-for can
    ;; return early on spurious pending-input, collapsing cycles to
    ;; ~1.2 s (smoke evidence); loop until the floor really elapsed.
    (while (< (float-time) (+ eipb4--t-cycle-start 4.0))
      (sit-for 0.5))))

;; ---------------- main ----------------

(message "=== EIPB P41 SOAK BEGIN (%s) ===" eipb4--build)
(ignore-errors (set-frame-height (selected-frame) 50))
(ignore-errors (set-frame-width  (selected-frame) 170))
(eipb4--append-line
 (format "EIPB4|%s|meta|emacs_pid|%d\n" eipb4--build (emacs-pid)))

(condition-case err
    (let* ((dir (file-name-as-directory (make-temp-file "eipb-soak" t)))
           (files (eipb4--make-project dir))
           (t-end (+ (float-time) eipb4--secs))
           (t-session (float-time))
           (widx 1) (w-start (float-time))
           (g-win0 gcs-done) (m-win0 (memory-limit))
           (cyc 1))
      (setq eipb4--dir dir
            eipb4--win-ms nil
            eipb4--win-cls (make-hash-table :test 'equal)
            eipb4--all-cls (make-hash-table :test 'equal)
            eipb4--head-all nil
            eipb4--tail-all nil
            eipb4--head-cls (make-hash-table :test 'equal)
            eipb4--tail-cls (make-hash-table :test 'equal)
            eipb4--bucket (min 600.0 (/ eipb4--secs 3.0))
            eipb4--ops 0
            eipb4--wins-done 0)
      (dolist (f files)
        (find-file f)
        (push (current-buffer) eipb4--buffers))
      (setq eipb4--buffers (nreverse eipb4--buffers)
            eipb4--t0 (float-time))
      (split-window-below)
      (other-window 1)
      (while (< (float-time) t-end)
        (setq eipb4--t-cycle-start (float-time))
        (condition-case ce
            (eipb4--cycle cyc)
          (error (eipb4--note "cycle" (format "err %S" ce))))
        (setq cyc (1+ cyc))
        (when (>= (- (float-time) w-start) eipb4--win-s)
          (eipb4--window-close widx g-win0 m-win0)
          (setq widx (1+ widx)
                w-start (float-time)
                g-win0 gcs-done
                m-win0 (memory-limit))))
      (eipb4--window-close widx g-win0 m-win0)
      ;; whole-run per-class dists
      (maphash
       (lambda (class ms)
         (let ((cell (concat "cls/" class)))
           (eipb4--emit cell "n" (float (length ms)))
           (dolist (p '(50 95 99))
             (eipb4--emit cell (format "p%d" p) (eipb4--pct ms p)))
           (eipb4--emit cell "max"
                        (car (last (sort (copy-sequence ms) #'<))))))
       eipb4--all-cls)
      ;; tail amplification -- HARD GATE metric
      (eipb4--emit-tail-amplification)
      ;; summary
      (eipb4--emit "meta" "wall_ms" (* (- (float-time) t-session) 1000.0))
      (eipb4--emit "meta" "ops" (float eipb4--ops))
      (eipb4--emit "meta" "cycles" (float (1- cyc)))
      (eipb4--emit "meta" "windows_completed" (float eipb4--wins-done))
      ;; teardown (instrumented; runner classifies exit)
      (eipb4--note "teardown" "killing-buffers")
      (switch-to-buffer (get-buffer-create "*scratch*"))
      (let (kill-buffer-query-functions)
        (dolist (b eipb4--buffers)
          (with-current-buffer b (set-buffer-modified-p nil))
          (ignore-errors (kill-buffer b))))
      (eipb4--note "teardown" "deleting-tree")
      (ignore-errors (delete-directory dir t))
      (eipb4--note "teardown" "done")
      (dolist (p (process-list))
        (eipb4--note "teardown"
                     (format "live-process %s %s"
                             (process-name p) (process-status p))))
      (eipb4--note "teardown" "exit-clean")
      (let ((confirm-kill-processes nil))
        (kill-emacs 0)))
  (error
   (eipb4--append-line
    (format "EIPB4|%s|FATAL|msg|SOAK ERROR: %S\n" eipb4--build err))
   (let ((confirm-kill-processes nil))
     (kill-emacs 1))))
