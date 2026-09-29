# claude-statusline

A fast, dependency-free status line for [Claude Code](https://claude.com/claude-code), written in C.

![The status line in a live Claude Code session](docs/live.png)

The same line in each state it can take, rendered from the real binary:

![Status line states](docs/example.png)

| Segment | Meaning |
|---|---|
| `4h25m:18%` | Time until the 5-hour rate-limit window resets, and how much of it is used. Hidden once the window has reset, because the reported usage then belongs to a window that no longer counts. |
| `Code/claude-statusline` | Last two components of the working directory. |
| `[main]` | Git branch. Hidden outside a repo and on a detached HEAD. |
| `Opus 5 (high)` | Model name (any ` (…)` suffix stripped) and the effort level, when set. |
| `ctx:12%` | Context window used. |

Percentages are green below 70, yellow from 70, red from 90. Control bytes in the directory, branch, model and effort text are stripped before printing.

## Why C

It runs on every render, so start-up cost matters. On this machine (200 renders, process spawn included) the C binary takes about 5 ms per render against about 27 ms for the equivalent shell script. It spawns no subprocesses: the JSON payload is parsed in-process, the branch is read straight from `.git/HEAD` (walking up from subdirectories and following the `gitdir:` file used by worktrees and submodules), and the clock comes from `time()`.

## Install

Needs `clang` (Xcode command line tools). The tests also need `jaq` (`brew install jaq`).

```sh
git clone <this repo> ~/Developer/Code/claude-statusline
~/Developer/Code/claude-statusline/build.sh
```

Then point Claude Code at the binary in `~/.claude/settings.json`:

```json
{
  "statusLine": {
    "type": "command",
    "command": "/Users/<you>/Developer/Code/claude-statusline/statusline"
  }
}
```

Restart Claude Code to pick it up.

## Layout

| Path | Purpose |
|---|---|
| `statusline.c` | The status line. |
| `json_mini.h` | Small structural JSON reader used by `statusline.c`. |
| `reference/statusline.sh` | The same behaviour as a shell script (needs `jaq`). It is the specification the tests hold the binary to. |
| `statusline.test.sh` | Differential tests: the binary must match the reference byte for byte, ANSI codes included. |
| `build.sh` | Builds the binary, then runs the tests. `--quick` skips the tests. |
| `tools/render-example.py` | Regenerates `docs/example.svg` and `docs/example.png` from the real binary's output. |

## Changing behaviour

Change `statusline.c` and `reference/statusline.sh` together, add a case to `statusline.test.sh`, and run `./build.sh`. The reference is what keeps the C port honest.

`python3 tools/render-example.py` regenerates `docs/example.svg` and `docs/example.png` from the built binary (the PNG needs Google Chrome on macOS). `docs/live.png` is a real screenshot and is not generated.

## License

Free for personal use, including by an individual using it on their own work machine. Copying, forking, redistributing and organization-wide deployment are not allowed. See [LICENSE](LICENSE).
