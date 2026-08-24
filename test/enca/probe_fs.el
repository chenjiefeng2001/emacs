(with-temp-file "/tmp/fs_probe.txt"
  (insert (format "py3=%s\n" (file-exists-p "/usr/bin/python3")))
  (insert (format "shim=%s\n" (file-exists-p "/tmp/fake_lsp.sh")))
  (insert (format "modes_py3=%s\n" (file-modes "/usr/bin/python3")))
  (insert (format "default-dir=%s\n" default-directory))
  (insert (format "exec-path-first=%s\n" (car exec-path))))
