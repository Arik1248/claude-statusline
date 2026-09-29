#!/usr/bin/env bash
# Claude Code status line — macOS (jaq, single JSON parse)
# Requires: jaq (brew install jaq), git
#
# NOT the live status line. statusLine.command points at the native ./statusline
# (~1.9ms vs ~31ms here). This file is kept deliberately as the REFERENCE
# IMPLEMENTATION that statusline.test.sh diffs the binary against — deleting it
# breaks that test suite.
set -u

JAQ="${JAQ:-/opt/homebrew/bin/jaq}"
command -v "$JAQ" >/dev/null 2>&1 || JAQ="$(command -v jaq 2>/dev/null || true)"
if [ -z "${JAQ}" ]; then
  printf '%s' 'statusline: jaq missing'
  exit 0
fi

# Claude Code feeds JSON on stdin — buffer once, parse once
input=$(cat)

# One parse → six fields: cwd, model, effort, ctx%, 5h%, 5h_reset_epoch
#
# Joined on US (0x1f), NOT tab. Tab counts as IFS *whitespace*, so bash `read`
# collapses runs of it into a single delimiter — meaning one empty field (no
# effort level, no rate-limit block) shifted every later value one slot left and
# the bar rendered e.g. the reset epoch as "ctx:1786100137%". US is not IFS
# whitespace, so empty fields are preserved.
IFS=$'\037' read -r cwd model thinking used five_used five_resets < <(
  printf '%s' "$input" | "$JAQ" -r '
    [
      (.cwd // ""),
      (.model.display_name // ""),
      (.effort.level // ""),
      (.context_window.used_percentage // ""),
      (.rate_limits.five_hour.used_percentage // ""),
      (.rate_limits.five_hour.resets_at // "")
    ] | map(tostring) | join("")
  ' 2>/dev/null
) || true

# Normalize Windows-style paths if any leak through
cwd=${cwd//\\//}

# Last two path components (pure bash)
short_cwd=""
if [ -n "$cwd" ]; then
  cwd=${cwd%/}
  base=${cwd##*/}
  parent=${cwd%/*}
  parent_base=${parent##*/}
  if [ -n "$parent" ] && [ "$parent" != "$cwd" ] && [ -n "$parent_base" ]; then
    short_cwd="${parent_base}/${base}"
  else
    short_cwd="$base"
  fi
fi

# Git branch — cheap, no optional locks
git_branch=""
if [ -n "$cwd" ] && [ -d "$cwd" ]; then
  git_branch=$(git -C "$cwd" --no-optional-locks symbolic-ref --short HEAD 2>/dev/null || true)
fi

GREEN=$'\033[0;32m'
YELLOW=$'\033[0;33m'
RED=$'\033[0;31m'
BLUE=$'\033[0;34m'
CYAN=$'\033[0;36m'
MAGENTA=$'\033[0;35m'
RESET=$'\033[0m'

color_pct() {
  local n=${1%%.*}
  n=${n:-0}
  if [ "$n" -ge 90 ] 2>/dev/null; then
    printf '%s' "$RED"
  elif [ "$n" -ge 70 ] 2>/dev/null; then
    printf '%s' "$YELLOW"
  else
    printf '%s' "$GREEN"
  fi
}

five_segment=""
# Hidden once resets_at has passed: used_percentage still describes the window
# that just ended, so showing it would report usage that no longer counts.
if [ -n "${five_used}" ] && [ -n "${five_resets}" ]; then
  used_int=${five_used%%.*}
  now=$(date +%s)
  remaining_secs=$((five_resets - now))
  if [ "$remaining_secs" -ge 3600 ] 2>/dev/null; then
    time_label="$((remaining_secs / 3600))h$(((remaining_secs % 3600) / 60))m"
  elif [ "$remaining_secs" -gt 0 ] 2>/dev/null; then
    time_label="$((remaining_secs / 60))m"
  else
    time_label=""
  fi
  [ -n "$time_label" ] && five_segment="$(color_pct "$used_int")${time_label}:${used_int}%${RESET}"
fi

if [ -n "$git_branch" ]; then
  dir_segment="${BLUE}${short_cwd}${RESET} ${CYAN}[${git_branch}]${RESET}"
else
  dir_segment="${BLUE}${short_cwd}${RESET}"
fi

# Strip " (…)" suffix from model display name
model_base=${model%% (*}
model_label=$model_base
[ -n "$thinking" ] && model_label="${model_base} (${thinking})"
model_segment="${MAGENTA}${model_label}${RESET}"

ctx_segment=""
if [ -n "$used" ]; then
  used_int=${used%%.*}
  ctx_segment="$(color_pct "$used_int")ctx:${used_int}%${RESET}"
fi

out=""
[ -n "$five_segment" ] && out="${five_segment} | "
out="${out}${dir_segment} | ${model_segment}"
[ -n "$ctx_segment" ] && out="${out} | ${ctx_segment}"
printf '%s' "$out"
