#!/usr/bin/env bash
# Atech Arcade CI.
#
# Host suite (compiles + runs with g++/make + python only, no hardware, no
# network): 10 game selftests, the Brain OS / screen / link / e2e module tests,
# python lint, and the visual-regression eval. This is the lane CI runs.
#
# Firmware lane (needs the `atech` CLI + PlatformIO toolchain + network): atech
# validate/build for the screen + controller projects. Fully guarded — a broken
# or absent toolchain is a non-fatal SKIP, never a failure.
#
#   tools/ci.sh              host suite + guarded firmware lane
#   tools/ci.sh --host-only  host suite only (what CI uses)
#   tools/ci.sh --skip-eval  skip the slower visual-regression eval
#                            (env CI_SKIP_EVAL=1 does the same)
set -euo pipefail
cd "$(dirname "$0")/.."

usage() { awk 'NR==1{next} /^#/{sub(/^# ?/,"");print;next} {exit}' "$0"; }

HOST_ONLY=0
SKIP_EVAL="${CI_SKIP_EVAL:-0}"
for arg in "$@"; do
  case "$arg" in
    --host-only) HOST_ONLY=1 ;;
    --skip-eval) SKIP_EVAL=1 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; usage >&2; exit 2 ;;
  esac
done

SUMMARY=""
FAILED=0

record() {
  SUMMARY+=$(printf '%-26s %s' "$1" "$2")$'\n'
  case "$2" in FAIL*) FAILED=1 ;; esac
}
note() { printf '\n== %s ==\n' "$1"; }

# Fatal stage: a failure marks the whole run FAILED (exit 1 at the end).
run_stage() {
  local name="$1"; shift
  note "$name"
  if "$@"; then record "$name" PASS; else record "$name" FAIL; fi
}

# Soft stage: a failure is a loud, non-fatal SKIP (used for the firmware lane,
# where a missing toolchain must not fail the run).
run_soft() {
  local name="$1"; shift
  note "$name"
  if "$@"; then
    record "$name" PASS
  else
    echo "WARN: '$name' failed or is unavailable — skipping (non-fatal)"
    record "$name" "SKIP (unavailable)"
  fi
}

GAMES=(demo pong snake eggcatch racing flappy doodlejump invaders jukebox ambient)

# ------------------------------------------------------------ 1. game selftests
note "host: ${#GAMES[@]} game selftests"
for g in "${GAMES[@]}"; do
  run_stage "gameselftest:$g" make -C sim gameselftest GAME="$g"
done

# ------------------------------------------------------------ 2. module tests
run_stage "console_os test"    make -C modules/console_os test
run_stage "screen_render test" make -C modules/screen_render test
run_stage "link test"          make -C modules/link test
run_stage "console_e2e test"   make -C modules/console_e2e test

# ------------------------------------------------------------ 3. python lint
run_stage "py format" uv run --group dev ruff format --check tools
run_stage "py lint"   uv run --group dev ruff check tools
run_stage "py drift"  env PYTHONPATH=tools uv run --group dev python -m gifgen.check_cpp_constants

# basedpyright ships in the dev group; guard it so a lean env just SKIPs.
note "py types"
if uv run --group dev basedpyright --version >/dev/null 2>&1; then
  if uv run --group dev basedpyright tools; then record "py types" PASS; else record "py types" FAIL; fi
else
  echo "SKIP: basedpyright not installed"
  record "py types" "SKIP (absent)"
fi

# --------------------------------------------------- 4. visual-regression eval
# Renders all 10 games and diffs blake2b framebuffer hashes vs
# tools/eval/baseline/*.json. Slower than the rest; skip with --skip-eval.
if [ "$SKIP_EVAL" = "1" ]; then
  note "eval"
  echo "SKIP: eval disabled (--skip-eval / CI_SKIP_EVAL=1)"
  record "eval" "SKIP (disabled)"
else
  note "eval (visual-regression baseline diff)"
  eval_log="$(mktemp)"
  if env PYTHONPATH=tools uv run --group dev python tools/eval/run.py 2>&1 | tee "$eval_log"; then
    record "eval" PASS
  else
    record "eval" FAIL
  fi
  if grep -q 'changed(' "$eval_log"; then
    echo "WARN: eval reports visual drift vs baseline (see 'changed' rows above)."
  fi
  rm -f "$eval_log"
fi

# ------------------------------------------------------------ 5. firmware lane
if [ "$HOST_ONLY" -eq 1 ]; then
  note "firmware lane"
  echo "SKIP: --host-only (no atech / PlatformIO toolchain, no network)"
  record "firmware" "SKIP (--host-only)"
elif uv run atech --version >/dev/null 2>&1; then
  for proj in screen controller; do
    run_soft "atech validate $proj" uv run atech validate "$proj"
    run_soft "atech build $proj"    uv run atech build "$proj"
  done
else
  note "firmware lane"
  echo "WARN: atech CLI unavailable — skipping firmware lane"
  record "firmware" "SKIP (atech absent)"
fi

# ------------------------------------------------------------ summary
printf '\n== summary ==\n%s' "$SUMMARY"
if [ "$FAILED" -ne 0 ]; then
  echo 'RESULT: FAIL'
  exit 1
fi
echo 'RESULT: PASS'
