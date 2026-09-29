/* Minimal JSON reader for statusline.c.
 *
 * Structural, not a substring scan — a naive search for a key would read values
 * out of the middle of another string (a path or model name containing
 * `"cwd"` or stray braces). */
#ifndef JSON_MINI_H
#define JSON_MINI_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ----------------------------------------------------------------- buffers */

typedef struct { char *p; size_t len, cap; } Buf;

static void buf_init(Buf *b) { b->cap = 256; b->len = 0; b->p = (char *)malloc(b->cap); b->p[0] = 0; }
static void buf_putn(Buf *b, const char *s, size_t n) {
    while (b->len + n + 1 > b->cap) { b->cap *= 2; b->p = (char *)realloc(b->p, b->cap); }
    memcpy(b->p + b->len, s, n); b->len += n; b->p[b->len] = 0;
}
static void buf_puts(Buf *b, const char *s) { buf_putn(b, s, strlen(s)); }
static void buf_putc(Buf *b, char c) { buf_putn(b, &c, 1); }

static char *read_all(FILE *f, size_t *out_len) {
    size_t cap = 8192, len = 0; char *p = (char *)malloc(cap);
    for (;;) {
        if (len + 4096 + 1 > cap) { cap *= 2; p = (char *)realloc(p, cap); }
        size_t n = fread(p + len, 1, 4096, f);
        len += n;
        if (n < 4096) break;
    }
    p[len] = 0; if (out_len) *out_len = len; return p;
}

/* ------------------------------------------------------------------ parser */

typedef struct { const char *p; size_t n; } Span;
typedef struct { const char *s; size_t i, n; } JP;

static void jm_skip_ws(JP *p) { while (p->i < p->n && isspace((unsigned char)p->s[p->i])) p->i++; }

static void jm_utf8(Buf *b, unsigned cp) {
    if (cp < 0x80) buf_putc(b, (char)cp);
    else if (cp < 0x800) { buf_putc(b, (char)(0xC0 | (cp >> 6))); buf_putc(b, (char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        buf_putc(b, (char)(0xE0 | (cp >> 12)));
        buf_putc(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        buf_putc(b, (char)(0x80 | (cp & 0x3F)));
    } else {
        buf_putc(b, (char)(0xF0 | (cp >> 18)));
        buf_putc(b, (char)(0x80 | ((cp >> 12) & 0x3F)));
        buf_putc(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        buf_putc(b, (char)(0x80 | (cp & 0x3F)));
    }
}

/* p->i must sit on '"'. Returns malloc'd decoded UTF-8. */
static char *jm_parse_string(JP *p) {
    if (p->i >= p->n || p->s[p->i] != '"') return NULL;
    p->i++;
    Buf b; buf_init(&b);
    while (p->i < p->n && p->s[p->i] != '"') {
        char c = p->s[p->i];
        if (c == '\\' && p->i + 1 < p->n) {
            p->i++;
            char e = p->s[p->i++];
            switch (e) {
                case 'n': buf_putc(&b, '\n'); break;
                case 't': buf_putc(&b, '\t'); break;
                case 'r': buf_putc(&b, '\r'); break;
                case 'b': buf_putc(&b, '\b'); break;
                case 'f': buf_putc(&b, '\f'); break;
                case '/': buf_putc(&b, '/');  break;
                case '"': buf_putc(&b, '"');  break;
                case '\\': buf_putc(&b, '\\'); break;
                case 'u': {
                    if (p->i + 4 <= p->n) {
                        char hex[5] = {0};
                        memcpy(hex, p->s + p->i, 4); p->i += 4;
                        unsigned cp = (unsigned)strtoul(hex, NULL, 16);
                        if (cp >= 0xD800 && cp <= 0xDBFF && p->i + 6 <= p->n &&
                            p->s[p->i] == '\\' && p->s[p->i + 1] == 'u') {
                            char hex2[5] = {0};
                            memcpy(hex2, p->s + p->i + 2, 4);
                            unsigned lo = (unsigned)strtoul(hex2, NULL, 16);
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                p->i += 6;
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            }
                        }
                        jm_utf8(&b, cp);
                    }
                    break;
                }
                default: buf_putc(&b, e); break;
            }
        } else { buf_putc(&b, c); p->i++; }
    }
    if (p->i < p->n) p->i++;
    return b.p;
}

static void jm_skip_value(JP *p) {
    jm_skip_ws(p);
    if (p->i >= p->n) return;
    char c = p->s[p->i];
    if (c == '"') { free(jm_parse_string(p)); return; }
    if (c == '{' || c == '[') {
        char open = c, close = (c == '{') ? '}' : ']';
        int depth = 0;
        while (p->i < p->n) {
            char d = p->s[p->i];
            if (d == '"') { free(jm_parse_string(p)); continue; }
            if (d == open) depth++;
            else if (d == close) { depth--; p->i++; if (depth == 0) return; continue; }
            p->i++;
        }
        return;
    }
    while (p->i < p->n && p->s[p->i] != ',' && p->s[p->i] != '}' && p->s[p->i] != ']') p->i++;
}

/* Raw span of the value for `key` in the object `obj`. n==0 when absent. */
static Span json_obj_get(Span obj, const char *key) {
    Span none = { NULL, 0 };
    JP p = { obj.p, 0, obj.n };
    jm_skip_ws(&p);
    if (p.i >= p.n || p.s[p.i] != '{') return none;
    p.i++;
    for (;;) {
        jm_skip_ws(&p);
        if (p.i >= p.n || p.s[p.i] == '}') return none;
        if (p.s[p.i] == ',') { p.i++; continue; }
        char *k = jm_parse_string(&p);
        if (!k) return none;
        jm_skip_ws(&p);
        if (p.i < p.n && p.s[p.i] == ':') p.i++;
        jm_skip_ws(&p);
        size_t start = p.i;
        jm_skip_value(&p);
        if (!strcmp(k, key)) { free(k); Span v = { obj.p + start, p.i - start }; return v; }
        free(k);
    }
}

/* Dotted path, e.g. "rate_limits.five_hour.resets_at". */
static Span json_path(Span root, const char *path) {
    char buf[256];
    snprintf(buf, sizeof buf, "%s", path);
    Span cur = root;
    char *save = NULL;
    for (char *tok = strtok_r(buf, ".", &save); tok; tok = strtok_r(NULL, ".", &save)) {
        cur = json_obj_get(cur, tok);
        if (!cur.n) return cur;
    }
    return cur;
}

/* Decoded string for a span. Strings are unescaped; numbers/literals copied raw.
 * Always returns malloc'd memory (empty string when absent). */
static char *json_span_str(Span v) {
    if (!v.n) return strdup("");
    if (v.p[0] == '"') { JP p = { v.p, 0, v.n }; char *s = jm_parse_string(&p); return s ? s : strdup(""); }
    size_t n = v.n;
    while (n && isspace((unsigned char)v.p[n-1])) n--;
    char *s = (char *)malloc(n + 1); memcpy(s, v.p, n); s[n] = 0; return s;
}

static char *json_get_str(Span root, const char *path) { return json_span_str(json_path(root, path)); }

#endif /* JSON_MINI_H */
