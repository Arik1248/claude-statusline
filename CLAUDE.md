# claude-statusline

Native (C) status line for Claude Code. `statusline.c` reads the status payload as JSON on stdin and prints one ANSI-coloured line. `reference/statusline.sh` is the same thing in shell and is the spec.

## Commands

- `./build.sh` — build `./statusline` and run the differential tests. `--quick` builds only.
- `bash statusline.test.sh` — tests only. Needs `jaq` and `python3`.
- `python3 tools/render-example.py` — regenerate `docs/example.svg` and `docs/example.png` from the built binary (PNG needs Chrome). `docs/live.png` is a hand-taken screenshot; do not overwrite it.

## Workflow

- Never push to `main`. Work on a branch, push it, and open a pull request. Only the repo owner approves and merges; `.github/CODEOWNERS` requires their review and `main` is protected.
- Run `./build.sh` before every push. For changes to `statusline.c` or `json_mini.h`, also fuzz an ASan+UBSan build (`clang -g -O1 -fsanitize=address,undefined`) with mutated and truncated payloads.

## Rules

- The binary and `reference/statusline.sh` must produce identical bytes for every payload. Any behaviour change goes into both, plus a case in `statusline.test.sh`. The differential test only proves the two agree; when a change is about behaviour rather than parity, also assert the output directly (see "stale window is hidden").
- Every string printed from the payload or the filesystem goes through `strip_ctrl` (and `strip_ctrl` in the reference). Keep it that way for anything new.
- No subprocesses in `statusline.c`. Branch comes from `.git/HEAD`, clock from `time()`. That is the reason the C version exists.
- `statusline` (the binary) is git-ignored. Rebuild after every edit to `statusline.c` or `json_mini.h`; Claude Code runs the binary, not the source.
- `~/.claude/settings.json` points `statusLine.command` at `<repo>/statusline`. Moving the repo means updating that path.
- Keep comments to non-obvious *why*. Rationale for a change belongs in the commit message.
