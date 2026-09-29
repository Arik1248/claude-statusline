// Native replacement for reference/statusline.sh.
//
// The shell version cost ~31ms per render (measured) and it renders constantly:
// bash start-up, then three subprocesses — jaq to parse the payload, `git
// symbolic-ref` for the branch, and `date` for the clock.
//
// This does the same work with zero subprocesses: the JSON is parsed in-process,
// the branch is read straight out of .git/HEAD (walking up for subdirectories,
// and following the `gitdir:` indirection used by worktrees and submodules),
// and the clock comes from time().
//
// Output is byte-identical to the shell version, ANSI codes included — verified
// by differential test in statusline.test.sh.
//
// Build: ./build.sh   (or: clang -O2 -Wall -o statusline statusline.c)
#include "json_mini.h"
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#define GREEN   "\033[0;32m"
#define YELLOW  "\033[0;33m"
#define RED     "\033[0;31m"
#define BLUE    "\033[0;34m"
#define CYAN    "\033[0;36m"
#define MAGENTA "\033[0;35m"
#define RESET   "\033[0m"

/* Integer part of a possibly-fractional string; non-numeric -> 0, matching the
 * shell's `[ "$n" -ge 90 ] 2>/dev/null` falling through to the default. */
static long int_part(const char *s) {
    if (!s || !*s) return 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s) return 0;
    return v;
}

static const char *color_pct(long n) {
    if (n >= 90) return RED;
    if (n >= 70) return YELLOW;
    return GREEN;
}

/* Drops C0 control bytes and DEL in place. Every string printed below comes from
 * the payload or the filesystem (a directory or model name can hold ESC), so
 * without this a crafted name could inject terminal escape sequences. */
static void strip_ctrl(char *s) {
    char *w = s;
    for (const char *r = s; *r; r++)
        if ((unsigned char)*r >= 0x20 && *r != 0x7f) *w++ = *r;
    *w = 0;
}

static int is_dir(const char *p) {
    struct stat st;
    return p && *p && stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Reads the first line of a file, without the trailing newline. */
static int read_line(const char *path, char *out, size_t cap) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    if (!fgets(out, (int)cap, f)) { fclose(f); return 0; }
    fclose(f);
    size_t n = strlen(out);
    while (n && (out[n-1] == '\n' || out[n-1] == '\r')) out[--n] = 0;
    return 1;
}

/* Equivalent of `git -C <cwd> symbolic-ref --short HEAD`, without spawning git.
 * Empty result for a detached HEAD (which is exactly what symbolic-ref does). */
static void git_branch(const char *cwd, char *out, size_t cap) {
    out[0] = 0;
    if (!is_dir(cwd)) return;

    char dir[4096];
    snprintf(dir, sizeof dir, "%s", cwd);

    for (;;) {
        char dotgit[4200];
        snprintf(dotgit, sizeof dotgit, "%s/.git", dir);

        struct stat st;
        if (stat(dotgit, &st) == 0) {
            char gitdir[4200];
            if (S_ISDIR(st.st_mode)) {
                snprintf(gitdir, sizeof gitdir, "%s", dotgit);
            } else {
                /* worktree / submodule: ".git" is a file holding "gitdir: <path>" */
                char line[4096];
                if (!read_line(dotgit, line, sizeof line)) return;
                const char *p = strstr(line, "gitdir:");
                if (!p) return;
                p += 7;
                while (*p == ' ') p++;
                if (*p == '/') snprintf(gitdir, sizeof gitdir, "%s", p);
                else snprintf(gitdir, sizeof gitdir, "%s/%s", dir, p);
            }

            char headp[4300];
            snprintf(headp, sizeof headp, "%s/HEAD", gitdir);
            char head[4096];
            if (!read_line(headp, head, sizeof head)) return;
            if (strncmp(head, "ref: ", 5) != 0) return;   /* detached HEAD */
            const char *ref = head + 5;
            if (!strncmp(ref, "refs/heads/", 11)) ref += 11;
            snprintf(out, cap, "%s", ref);
            return;
        }

        /* walk up */
        char *slash = strrchr(dir, '/');
        if (!slash || slash == dir) return;
        *slash = 0;
    }
}

int main(void) {
    size_t len = 0;
    char *raw = read_all(stdin, &len);
    Span root = { raw, len };

    char *cwd          = json_get_str(root, "cwd");
    char *model        = json_get_str(root, "model.display_name");
    char *thinking     = json_get_str(root, "effort.level");
    char *used         = json_get_str(root, "context_window.used_percentage");
    char *five_used    = json_get_str(root, "rate_limits.five_hour.used_percentage");
    char *five_resets  = json_get_str(root, "rate_limits.five_hour.resets_at");

    for (char *q = cwd; *q; q++) if (*q == '\\') *q = '/';   /* normalize stray win paths */

    /* last two path components */
    char short_cwd[4096]; short_cwd[0] = 0;
    if (*cwd) {
        char t[4096];
        snprintf(t, sizeof t, "%s", cwd);
        size_t n = strlen(t);
        if (n > 0 && t[n-1] == '/') t[n-1] = 0;             /* ${cwd%/} */
        char *last = strrchr(t, '/');
        const char *base = last ? last + 1 : t;
        char parent[4096]; parent[0] = 0;
        if (last) { size_t pl = (size_t)(last - t); memcpy(parent, t, pl); parent[pl] = 0; }
        char *pslash = strrchr(parent, '/');
        const char *parent_base = pslash ? pslash + 1 : parent;
        if (*parent && strcmp(parent, t) != 0 && *parent_base)
            snprintf(short_cwd, sizeof short_cwd, "%s/%s", parent_base, base);
        else
            snprintf(short_cwd, sizeof short_cwd, "%s", base);
    }

    char branch[512];
    git_branch(cwd, branch, sizeof branch);
    strip_ctrl(short_cwd);
    strip_ctrl(branch);
    strip_ctrl(thinking);

    Buf out; buf_init(&out);

    /* 5-hour rate-limit segment. Hidden once resets_at has passed: the payload's
     * used_percentage still describes the window that just ended, so showing it
     * (as "0m:NN%") would report usage that no longer counts. */
    long now = (long)time(NULL);
    long resets = *five_resets ? strtol(five_resets, NULL, 10) : 0;
    long remaining = resets > now ? resets - now : 0;   /* no signed overflow on absurd input */
    if (*five_used && *five_resets && remaining > 0) {
        long used_int = int_part(five_used);
        char label[64];
        if (remaining >= 3600)     snprintf(label, sizeof label, "%ldh%ldm", remaining / 3600, (remaining % 3600) / 60);
        else                       snprintf(label, sizeof label, "%ldm", remaining / 60);
        char seg[256];
        snprintf(seg, sizeof seg, "%s%s:%ld%%%s | ", color_pct(used_int), label, used_int, RESET);
        buf_puts(&out, seg);
    }

    /* directory + branch */
    buf_puts(&out, BLUE); buf_puts(&out, short_cwd); buf_puts(&out, RESET);
    if (*branch) {
        buf_puts(&out, " " CYAN "["); buf_puts(&out, branch); buf_puts(&out, "]" RESET);
    }

    /* model, with " (…)" suffix stripped and effort appended */
    buf_puts(&out, " | " MAGENTA);
    char model_base[256];
    snprintf(model_base, sizeof model_base, "%s", model);
    char *paren = strstr(model_base, " (");
    if (paren) *paren = 0;
    strip_ctrl(model_base);
    buf_puts(&out, model_base);
    if (*thinking) { buf_puts(&out, " ("); buf_puts(&out, thinking); buf_putc(&out, ')'); }
    buf_puts(&out, RESET);

    /* context window */
    if (*used) {
        long ui = int_part(used);
        char seg[128];
        snprintf(seg, sizeof seg, " | %sctx:%ld%%%s", color_pct(ui), ui, RESET);
        buf_puts(&out, seg);
    }

    fwrite(out.p, 1, out.len, stdout);
    return 0;
}
