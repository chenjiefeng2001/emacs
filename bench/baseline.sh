#!/bin/sh
set -eu

out_dir="bench/results"
mkdir -p "$out_dir"
ts=$(date +%Y%m%d-%H%M%S)
out="$out_dir/baseline-$ts.txt"

cfg_opt=${ENCA_CONFIGURE_OPTIONS:-}
if [ -z "$cfg_opt" ] && [ -f config.log ]; then
  cfg_opt=$(sed -n 's/^\$ //p' config.log | head -n1)
fi
[ -z "$cfg_opt" ] && cfg_opt="not-configured"

build_flags=${ENCA_BUILD_FLAGS:-}
if [ -z "$build_flags" ]; then
  MK=$(command -v make || command -v mingw32-make || true)
  if [ -n "${MK:-}" ]; then
    cmd=$("$MK" --no-print-directory -C test/enca -n gcc-check 2>/dev/null \
      | tr -d '\\\t\n\r' | tr -s ' ')
    build_flags=$(printf '%s' "$cmd" \
      | sed 's/^\(.*\) -o \([^ ]*\) .*/\1 -o \2 <sources per test\/enca\/Makefile>/')
  fi
fi
[ -z "$build_flags" ] && build_flags="<fill from actual build>"
gc_settings=${ENCA_GC_SETTINGS:-default}

cc_bin=${CC:-}
if [ -z "$cc_bin" ]; then
  for c in cc gcc clang; do
    if command -v "$c" >/dev/null 2>&1; then
      cc_bin=$c
      break
    fi
  done
fi
compiler_c=unknown
if [ -n "$cc_bin" ]; then
  compiler_c=$("$cc_bin" --version 2>/dev/null | head -n1 || echo unknown)
fi

{
  echo "enca-baseline-record"
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "emacs-version: $(sed -n 's/^AC_INIT(\[GNU Emacs\], \[\([^]]*\)\].*/\1/p' configure.ac | head -n1)"
  echo "git-revision: $(git rev-parse HEAD)"
  echo "git-describe: $(git describe --always --dirty 2>/dev/null || echo unknown)"
  echo "compiler-c: $compiler_c"
  echo "configure-options: $cfg_opt"
  echo "cpu: $(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2 | sed 's/^ //' || sysctl -n hw.model 2>/dev/null || echo unknown)"
  echo "os: $(uname -srm)"
  echo "build-flags: $build_flags"
  echo "gc-settings: $gc_settings"
} > "$out"

echo "wrote $out"
