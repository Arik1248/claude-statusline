# Security

## Reporting

Please report vulnerabilities privately through GitHub: **Security → Report a vulnerability** on this repository. Do not open a public issue for them.

## Scope

`statusline` reads one JSON payload from stdin (written by Claude Code) and a repository's `.git/HEAD`, and prints one line. It makes no network calls, spawns no subprocesses and writes no files.

Everything it prints that originates in the payload or on disk (directory name, branch, model name, effort level) has control bytes stripped, so a crafted name cannot inject terminal escape sequences.

The parser is fuzzed under AddressSanitizer and UndefinedBehaviorSanitizer before changes to `statusline.c` or `json_mini.h` are merged.
