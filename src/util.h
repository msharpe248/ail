#ifndef AIL_UTIL_H
#define AIL_UTIL_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *file;
    int line;
    int col;
} Span;

/* ---- Arena allocator ---- */

typedef struct ArenaChunk {
    struct ArenaChunk *next;
    size_t used;
    size_t cap;
    unsigned char data[];
} ArenaChunk;

typedef struct {
    ArenaChunk *head;
} Arena;

void arena_init(Arena *a);
void *arena_alloc(Arena *a, size_t size);
char *arena_strndup(Arena *a, const char *s, size_t n);
char *arena_strdup(Arena *a, const char *s);
void arena_free(Arena *a);

/* ---- Growable string buffer ---- */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

void sb_init(StrBuf *sb);
void sb_free(StrBuf *sb);
void sb_append_n(StrBuf *sb, const char *s, size_t n);
void sb_append(StrBuf *sb, const char *s);
void sb_append_ch(StrBuf *sb, char c);
void sb_printf(StrBuf *sb, const char *fmt, ...);
void sb_append_escaped(StrBuf *sb, const char *s, size_t n);

/* ---- Diagnostics ---- */

typedef enum {
    DIAG_ERROR,
    DIAG_WARN
} DiagLevel;

typedef struct {
    DiagLevel level;
    Span span;
    char *msg;
} Diag;

typedef struct {
    Diag *items;
    size_t len;
    size_t cap;
    size_t error_count;
    size_t warn_count;
} DiagList;

void diag_init(DiagList *dl);
void diag_free(DiagList *dl);
void diag_error(DiagList *dl, Span span, const char *fmt, ...);
void diag_warn(DiagList *dl, Span span, const char *fmt, ...);
void diag_print(const DiagList *dl);

/* ---- File helpers ---- */

char *read_entire_file(const char *path, size_t *out_len);
bool write_entire_file(const char *path, const char *data, size_t len);

#endif
