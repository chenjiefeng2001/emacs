#!/bin/bash
# P0 Baseline Closure -- create three build trees from IDENTICAL inputs.
#   A vanilla : upstream commit 11a1cb7445d, zero ENCA content
#   B disabled: current fork source, configure WITHOUT --enable-enca
#   C enabled : current fork source, configure WITH    --enable-enca
# All three share the exact same configure line except the enca flag,
# and the exact same CFLAGS, so any delta is attributable.
set -e
BASE=11a1cb7445d
SRC=/mnt/c/Users/14977/source/repos/emacs
CONF_COMMON="--without-x --without-all --disable-build-details"
CFLAGS="-O2"

echo "== export vanilla ($BASE) =="
rm -rf ~/enca-p0-a
mkdir -p ~/enca-p0-a
cd $SRC && git archive $BASE | tar -x -C ~/enca-p0-a

echo "== export fork snapshots =="
for t in b c; do
  rm -rf ~/enca-p0-$t
  mkdir -p ~/enca-p0-$t
  git archive HEAD | tar -x -C ~/enca-p0-$t
done

configure_tree () {
  local dir=$1 extra=$2
  cd $dir
  if [ ! -f ./configure ]; then
    echo "AUTOGEN $dir"
    ./autogen.sh > autogen.log 2>&1 \
      || { echo "AUTOGEN FAIL $dir"; tail -20 autogen.log; exit 1; }
  fi
  ./configure $CONF_COMMON $extra CFLAGS="$CFLAGS" > configure.log 2>&1 \
    && echo "CONFIGURE OK $dir ($extra)" \
    || { echo "CONFIGURE FAIL $dir"; tail -20 configure.log; exit 1; }
}

configure_tree ~/enca-p0-a ""
configure_tree ~/enca-p0-b ""
configure_tree ~/enca-p0-c "--enable-enca"

grep -c "HAVE_ENCA" ~/enca-p0-a/src/config.h 2>/dev/null || echo "A: no HAVE_ENCA (expected)"
grep "^ENCA_OBJ" ~/enca-p0-b/src/Makefile || echo "B: ENCA_OBJ empty (expected)"
grep "^ENCA_OBJ" ~/enca-p0-c/src/Makefile | head -c 120; echo " ... (C: expected non-empty)"
