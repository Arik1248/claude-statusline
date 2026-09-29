# claude-statusline

Native (C) status line for Claude Code. `statusline.c` reads the status payload as JSON on stdin and prints one ANSI-coloured line. `reference/statusline.sh` is the same thing in shell and is the spec.

## Commands

- `./build.sh` — build `./statusline` and run the differential tests. `--quick` builds only.
- `bash statusline.test.sh` — tests only. Needs `jaq` and `python3`.
- `python3 tools/render-example.py` — regenerate `docs/example.svg` from the built binary.

## Rules

- The binary and `reference/statusline.sh` must produce identical bytes for every payload. Any behaviour change goes into both, plus a case in `statusline.test.sh`. The differential test only proves the two agree; when a change is about behaviour rather than parity, also assert the output directly (see "stale window is hidden").
- No subprocesses in `statusline.c`. Branch comes from `.git/HEAD`, clock from `time()`. That is the reason the C version exists.
- `statusline` (the binary) is git-ignored. Rebuild after every edit to `statusline.c` or `json_mini.h`; Claude Code runs the binary, not the source.
- `~/.claude/settings.json` points `statusLine.command` at `<repo>/statusline`. Moving the repo means updating that path.
- Keep comments to non-obvious *why*. Rationale for a change belongs in the commit message.
