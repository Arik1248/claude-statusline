#!/usr/bin/env bash
# Differential test: native ./statusline must byte-match statusline-command.sh,
# ANSI codes included, for every payload shape.
set -u
B="$(cd "$(dirname "$0")" && pwd)"
NATIVE="$B/statusline"
SHELL_IMPL="$B/reference/statusline.sh"
FAIL=0; PASSN=0

t() {
  local name="$1" payload="$2"
  local a b
  a=$(printf '%s' "$payload" | "$NATIVE" | od -c | head -40)
  b=$(printf '%s' "$payload" | bash "$SHELL_IMPL" | od -c | head -40)
  if [ "$a" = "$b" ]; then m="ok  "; PASSN=$((PASSN+1)); else m="FAIL"; FAIL=$((FAIL+1)); fi
  printf '%s %s\n' "$m" "$name"
  if [ "$a" != "$b" ]; then
    printf '     native: %s\n' "$(printf '%s' "$payload" | "$NATIVE" | cat -v)"
    printf '     shell : %s\n' "$(printf '%s' "$payload" | bash "$SHELL_IMPL" | cat -v)"
  fi
}

# resets_at far in the future keeps the h/m label stable between the two runs
NOW=$(date +%s)
FUTURE=$((NOW + 5430))   # +30s so a tick between the two runs cannot cross a minute boundary
SOON=$((NOW + 630))
PAST=$((NOW - 60))
REPO="$B"   # this repo: a real git checkout with a branch

echo "=== full payloads ==="
t "full, in a git repo" "{\"cwd\":\"$REPO\",\"model\":{\"display_name\":\"Opus 5\"},\"effort\":{\"level\":\"xhigh\"},\"context_window\":{\"used_percentage\":42.5},\"rate_limits\":{\"five_hour\":{\"used_percentage\":31,\"resets_at\":$FUTURE}}}"
t "non-git dir"          "{\"cwd\":\"/tmp\",\"model\":{\"display_name\":\"Opus 5\"},\"effort\":{\"level\":\"high\"},\"context_window\":{\"used_percentage\":10}}"
t "no effort"            "{\"cwd\":\"$REPO\",\"model\":{\"display_name\":\"Sonnet 5\"},\"context_window\":{\"used_percentage\":5}}"
t "model with paren"     "{\"cwd\":\"/tmp\",\"model\":{\"display_name\":\"Opus 5 (1m context)\"},\"effort\":{\"level\":\"low\"}}"

echo
echo "=== percentage colour thresholds ==="
t "ctx 69 (green)"       "{\"cwd\":\"/tmp\",\"model\":{\"display_name\":\"M\"},\"context_window\":{\"used_percentage\":69}}"
t "ctx 70 (yellow)"      "{\"cwd\":\"/tmp\",\"model\":{\"display_name\":\"M\"},\"context_window\":{\"used_percentage\":70}}"
t "ctx 89 (yellow)"      "{\"cwd\":\"/tmp\",\"model\":{\"display_name\":\"M\"},\"context_window\":{\"used_percentage\":89}}"
t "ctx 90 (red)"         "{\"cwd\":\"/tmp\",\"model\":{\"display_name\":\"M\"},\"context_window\":{\"used_percentage\":90}}"
t "ctx 100 (red)"        "{\"cwd\":\"/tmp\",\"model\":{\"display_name\":\"M\"},\"context_window\":{\"used_percentage\":100}}"
t "ctx 0"                "{\"cwd\":\"/tmp\",\"model\":{\"display_name\":\"M\"},\"context_window\":{\"used_percentage\":0}}"

echo
echo "=== rate-limit time labels ==="
t "resets >1h"           "{\"cwd\":\"/tmp\",\"model\":{\"display_name\":\"M\"},\"rate_limits\":{\"five_hour\":{\"used_percentage\":50,\"resets_at\":$FUTURE}}}"
t "resets <1h"           "{\"cwd\":\"/tmp\",\"model\":{\"display_name\":\"M\"},\"rate_limits\":{\"five_hour\":{\"used_percentage\":50,\"resets_at\":$SOON}}}"
t "resets in past"       "{\"cwd\":\"/tmp\",\"model\":{\"display_name\":\"M\"},\"rate_limits\":{\"five_hour\":{\"used_percentage\":95,\"resets_at\":$PAST}}}"

# The differential test only proves native == shell; this pins the behaviour itself.
stale=$(printf '{"cwd":"/tmp","model":{"display_name":"M"},"rate_limits":{"five_hour":{"used_percentage":95,"resets_at":%s}}}' "$PAST" | "$NATIVE" | cat -v)
case "$stale" in
  *"95%"*|*"0m:"*) FAIL=$((FAIL+1)); printf 'FAIL stale window is hidden\n     got: %s\n' "$stale" ;;
  *)               PASSN=$((PASSN+1)); printf 'ok   stale window is hidden\n' ;;
esac
t "five_used no resets"  "{\"cwd\":\"/tmp\",\"model\":{\"display_name\":\"M\"},\"rate_limits\":{\"five_hour\":{\"used_percentage\":50}}}"

echo
echo "=== cwd shapes ==="
t "root cwd"             "{\"cwd\":\"/\",\"model\":{\"display_name\":\"M\"}}"
t "single component"     "{\"cwd\":\"/tmp\",\"model\":{\"display_name\":\"M\"}}"
t "trailing slash"       "{\"cwd\":\"/opt/homebrew/\",\"model\":{\"display_name\":\"M\"}}"
t "deep path"            "{\"cwd\":\"/opt/homebrew/Cellar\",\"model\":{\"display_name\":\"M\"}}"
t "nonexistent dir"      "{\"cwd\":\"/no/such/place/here\",\"model\":{\"display_name\":\"M\"}}"
t "empty cwd"            "{\"model\":{\"display_name\":\"M\"}}"
t "cwd with space"       "{\"cwd\":\"/tmp\",\"model\":{\"display_name\":\"My Model\"}}"

echo
echo "=== degenerate input ==="
t "empty object"         "{}"
t "malformed"            "{not json"

echo
echo "=== hostile input ==="
# ESC and BEL inside a model name, effort level and directory name must not reach the terminal.
t "control bytes in model/effort" '{"cwd":"/tmp","model":{"display_name":"Evil\u001b]0;pwned\u0007M"},"effort":{"level":"x\u001b[2Jy"}}'
EVILROOT=$(mktemp -d)
mkdir -p "$EVILROOT/$(printf 'a\033]0;pwned\007b')"
t "control bytes in directory"  "{\"cwd\":\"$EVILROOT/a\\u001b]0;pwned\\u0007b\",\"model\":{\"display_name\":\"M\"}}"
rm -rf "$EVILROOT"
t "absurd resets_at (negative)" '{"cwd":"/tmp","model":{"display_name":"M"},"rate_limits":{"five_hour":{"used_percentage":50,"resets_at":-99999999999999999999}}}'

# Parity with the shell proves nothing if both leak, so assert the property directly:
# the only ESC bytes in the output are our own colour codes ("ESC[0;3Nm" / "ESC[0m").
leak=$(printf '%s' '{"cwd":"/tmp","model":{"display_name":"Evil\u001b]0;pwned\u0007M"},"effort":{"level":"x\u001b[2Jy"}}' | "$NATIVE" | LC_ALL=C sed -E $'s/\033\\[0(;3[0-9])?m//g' | LC_ALL=C tr -d '[:print:]')
if [ -z "$leak" ]; then PASSN=$((PASSN+1)); echo "ok   no control bytes leak through"; else FAIL=$((FAIL+1)); echo "FAIL no control bytes leak through"; fi

echo
echo "passed=$PASSN failed=$FAIL"
[ $FAIL -eq 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $([ $FAIL -eq 0 ] && echo 0 || echo 1)
