#!/usr/bin/env bash
# Repo CI: symlink sanity, atech validate/build for both projects, sim selftest.
set -euo pipefail
cd "$(dirname "$0")/.."

SUMMARY=""
FAILED=0

record() {
  SUMMARY+=$(printf '%-22s %s' "$1" "$2")$'\n'
  case "$2" in FAIL*) FAILED=1 ;; esac
}

note() { printf '\n== %s ==\n' "$1"; }

run_stage() {
  local name="$1"; shift
  note "$name"
  if "$@"; then record "$name" PASS; else record "$name" FAIL; fi
}

note "symlinks"
sym_fail=0
for f in pong_proto.h pong_link.h net_config.h pong_shared.h; do
  p="modules/pong_control/$f"
  if t=$(readlink -f "$p" 2>/dev/null) && [ -r "$t" ]; then
    echo "ok: $p -> $t"
  else
    echo "ERROR: $p does not resolve (expected symlink to ../pong_screen/$f)" >&2
    sym_fail=1
  fi
done
[ "$sym_fail" -eq 0 ] || { echo "FAIL: shared-header symlinks broken" >&2; exit 1; }
record "symlinks" PASS

run_stage "validate screen" uv run atech validate screen
run_stage "validate controller" uv run atech validate controller

screen_missing=""
for f in pong_engine.cpp pong_screen.cpp; do
  [ -f "modules/pong_screen/$f" ] || screen_missing="$screen_missing $f"
done
if [ -n "$screen_missing" ]; then
  note "build screen"
  echo "WARN: skipping — missing:$screen_missing"
  record "build screen" "SKIP (missing:$screen_missing)"
else
  run_stage "build screen" uv run atech build screen
fi

controller_missing=""
for f in pong_control.cpp audio_director.cpp score_display.cpp ring_fx.cpp; do
  [ -f "modules/pong_control/$f" ] || controller_missing="$controller_missing $f"
done
if [ -n "$controller_missing" ]; then
  note "build controller"
  echo "WARN: skipping — missing:$controller_missing"
  record "build controller" "SKIP (missing:$controller_missing)"
else
  run_stage "build controller" uv run atech build controller
fi

if [ -f sim/Makefile ]; then
  note "sim build"
  if make -C sim; then
    record "sim build" PASS
    run_stage "sim selftest" ./sim/pong_sim --selftest
  else
    record "sim build" FAIL
    record "sim selftest" "SKIP (build failed)"
  fi
else
  note "sim"
  echo "WARN: sim/Makefile not found — skipping"
  record "sim build" "SKIP (no sim/Makefile)"
  record "sim selftest" "SKIP (no sim/Makefile)"
fi

run_stage "py format" uv run --group dev ruff format --check tools
run_stage "py drift" env PYTHONPATH=tools uv run --group dev python -m gifgen.check_cpp_constants

printf '\n== summary ==\n%s' "$SUMMARY"
if [ "$FAILED" -ne 0 ]; then
  echo 'RESULT: FAIL'
  exit 1
fi
echo 'RESULT: PASS'
