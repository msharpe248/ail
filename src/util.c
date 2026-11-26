#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Arena ---- */

void arena_init(Arena *a) {
    a->head = NULL;
}

void *arena_alloc(Arena *a, size_t size) {
    size = (size + 7u) & ~7u;
    if (size == 0) size = 8;
    ArenaChunk *c = a->head;
    if (!c || c->used + size > c->cap) {
        size_t cap = size > 65536 ? size : 65536;
        c = (ArenaChunk *)malloc(sizeof(ArenaChunk) + cap);
        if (!c) {
            fprintf(stderr, "ail: out of memory\n");
            exit(1);
        }
        c->next = a->head;
        c->used = 0;
        c->cap = cap;
        a->head = c;
    }
    void *ptr = c->data + c->used;
    memset(ptr, 0, size);
    c->used += size;
    return ptr;
}

char *arena_strndup(Arena *a, const char *s, size_t n) {
    char *p = (char *)arena_alloc(a, n + 1);
    if (n > 0) memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *arena_strdup(Arena *a, const char *s) {
    return arena_strndup(a, s, strlen(s));
}

void arena_free(Arena *a) {
    ArenaChunk *c = a->head;
    while (c) {
        ArenaChunk *next = c->next;
        free(c);
        c = next;
    }
    a->head = NULL;
}

/* ---- StrBuf ---- */

void sb_init(StrBuf *sb) {
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void sb_free(StrBuf *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void sb_grow(StrBuf *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) return;
    size_t ncap = sb->cap ? sb->cap * 2 : 256;
    while (ncap < sb->len + extra + 1) ncap *= 2;
    char *ndata = (char *)realloc(sb->data, ncap);
    if (!ndata) {
        fprintf(stderr, "ail: out of memory\n");
        exit(1);
    }
    sb->data = ndata;
    sb->cap = ncap;
}

void sb_append_n(StrBuf *sb, const char *s, size_t n) {
    sb_grow(sb, n);
    if (n > 0) memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

void sb_append(StrBuf *sb, const char *s) {
    sb_append_n(sb, s, strlen(s));
}

void sb_append_ch(StrBuf *sb, char c) {
    sb_grow(sb, 1);
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

void sb_printf(StrBuf *sb, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need > 0) {
        sb_grow(sb, (size_t)need);
        vsnprintf(sb->data + sb->len, (size_t)need + 1, fmt, ap2);
        sb->len += (size_t)need;
    }
    va_end(ap2);
}

void sb_append_escaped(StrBuf *sb, const char *s, size_t n) {
    sb_append_ch(sb, '"');
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)s[i];
        switch (ch) {
            case '"':  sb_append(sb, "\\\""); break;
            case '\\': sb_append(sb, "\\\\"); break;
            case '\n': sb_append(sb, "\\n"); break;
            case '\r': sb_append(sb, "\\r"); break;
            case '\t': sb_append(sb, "\\t"); break;
            case '\0': sb_append(sb, "\\0"); break;
            default:
                if (ch < 0x20) {
                    sb_printf(sb, "\\x%02x", ch);
                } else {
                    sb_append_ch(sb, (char)ch);
                }
                break;
        }
    }
    sb_append_ch(sb, '"');
}

/* ---- Diagnostics ---- */

void diag_init(DiagList *dl) {
    dl->items = NULL;
    dl->len = 0;
    dl->cap = 0;
    dl->error_count = 0;
    dl->warn_count = 0;
}

void diag_free(DiagList *dl) {
    for (size_t i = 0; i < dl->len; i++) free(dl->items[i].msg);
    free(dl->items);
    dl->items = NULL;
    dl->len = 0;
    dl->cap = 0;
}

static void diag_add(DiagList *dl, DiagLevel lvl, Span span, const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int need = vsnprintf(NULL, 0, fmt, ap);
    char *msg = (char *)malloc((size_t)(need > 0 ? need : 0) + 1);
    if (!msg) exit(1);
    vsnprintf(msg, (size_t)(need > 0 ? need : 0) + 1, fmt, ap2);
    va_end(ap2);

    if (dl->len == dl->cap) {
        dl->cap = dl->cap ? dl->cap * 2 : 16;
        dl->items = (Diag *)realloc(dl->items, dl->cap * sizeof(Diag));
        if (!dl->items) exit(1);
    }
    dl->items[dl->len++] = (Diag){ .level = lvl, .span = span, .msg = msg };
    if (lvl == DIAG_ERROR) dl->error_count++;
    else dl->warn_count++;
}

void diag_error(DiagList *dl, Span span, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    diag_add(dl, DIAG_ERROR, span, fmt, ap);
    va_end(ap);
}

void diag_warn(DiagList *dl, Span span, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    diag_add(dl, DIAG_WARN, span, fmt, ap);
    va_end(ap);
}

void diag_print(const DiagList *dl) {
    for (size_t i = 0; i < dl->len; i++) {
        const Diag *d = &dl->items[i];
        fprintf(stderr, "%s:%d:%d: %s: %s\n",
                d->span.file ? d->span.file : "<input>",
                d->span.line, d->span.col,
                d->level == DIAG_ERROR ? "error" : "warning",
                d->msg);
    }
}

/* ---- File I/O ---- */

char *read_entire_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    if (out_len) *out_len = got;
    return buf;
}

bool write_entire_file(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t put = fwrite(data, 1, len, f);
    int rc = fclose(f);
    return put == len && rc == 0;
}
