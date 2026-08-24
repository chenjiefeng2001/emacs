#!/bin/bash
# EVS-5.2.6: sync ENCA sources and rebuild (idempotent OBJ patches).
set -e
cd ~/enca-p11
cp /mnt/c/Users/14977/source/repos/emacs/src/enca-evs.c src/
rm -rf src/enca
cp -r /mnt/c/Users/14977/source/repos/emacs/src/enca src/enca

add_obj () {  # add_obj <pattern-anchor> <objects-to-append>
  if ! grep -q "$2" src/Makefile; then
    sed -i "s|$1|$1 $2|" src/Makefile
  fi
}
grep -q 'enca/wake/wake\.o'           src/Makefile || add_obj 'enca/scheduler/scheduler\.o' 'enca/wake/wake.o'
grep -q 'enca/completion/completion\.o' src/Makefile || add_obj 'enca/wake/wake\.o'          'enca/completion/completion.o'
grep -q 'enca/completion/cache\.o'    src/Makefile || add_obj 'enca/completion/completion\.o' 'enca/completion/cache.o'
grep -q 'enca/lsp/session\.o'         src/Makefile || add_obj 'enca/completion/cache\.o'      'enca/lsp/jsonrpc.o enca/lsp/transport.o enca/lsp/session.o'

if make -j20 > build.log 2>&1; then
  echo BUILD_OK
else
  echo BUILD_FAIL
  grep -E 'error:|undefined' build.log | head -20
  exit 1
fi
