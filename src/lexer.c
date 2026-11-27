#include "lexer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *file;
    const char *src;
    size_t len;
    size_t pos;
    int line;
    int col;
    Arena *arena;
    DiagList *diags;
    TokenList *out;
    TokKind prev;

    /* Interpolation brace stack */
    int interp_depth;
    int interp_braces[32];
    bool interp_heredoc[32];
    char interp_tag[32][64];
} Lexer;

static void push_tok(Lexer *lx, Token t) {
    if (lx->out->len == lx->out->cap) {
        lx->out->cap = lx->out->cap ? lx->out->cap * 2 : 256;
        lx->out->items = (Token *)realloc(lx->out->items, lx->out->cap * sizeof(Token));
        if (!lx->out->items) exit(1);
    }
    lx->out->items[lx->out->len++] = t;
    lx->prev = t.kind;
}

static char peek(const Lexer *lx, size_t off) {
    return (lx->pos + off < lx->len) ? lx->src[lx->pos + off] : '\0';
}

static char advance(Lexer *lx) {
    if (lx->pos >= lx->len) return '\0';
    char c = lx->src[lx->pos++];
    if (c == '\n') { lx->line++; lx->col = 1; }
    else { lx->col++; }
    return c;
}

static Span cur_span(const Lexer *lx) {
    return (Span){ .file = lx->file, .line = lx->line, .col = lx->col };
}

static bool is_ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static bool is_ident_cont(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

/* Can '/' start a regex literal right after prev token? */
static bool can_start_regex(TokKind prev) {
    switch (prev) {
        case TOK_INT: case TOK_FLOAT: case TOK_DECIMAL:
        case TOK_STRING: case TOK_STR_TAIL: case TOK_SYMBOL: case TOK_REGEX:
        case TOK_IDENT: case TOK_IVAR: case TOK_SELF: case TOK_NIL:
        case TOK_TRUE: case TOK_FALSE: case TOK_RPAREN: case TOK_RBRACK: case TOK_RBRACE:
            return false;
        default:
            return true;
    }
}

static TokKind lookup_keyword(const char *s, size_t n) {
    static const struct { const char *kw; TokKind kind; } kws[] = {
        {"def", TOK_DEF}, {"end", TOK_END}, {"class", TOK_CLASS}, {"module", TOK_MODULE},
        {"include", TOK_INCLUDE}, {"if", TOK_IF}, {"elsif", TOK_ELSIF}, {"else", TOK_ELSE},
        {"unless", TOK_UNLESS}, {"case", TOK_CASE}, {"when", TOK_WHEN},
        {"while", TOK_WHILE}, {"until", TOK_UNTIL}, {"for", TOK_FOR}, {"in", TOK_IN},
        {"do", TOK_DO}, {"begin", TOK_BEGIN}, {"rescue", TOK_RESCUE}, {"ensure", TOK_ENSURE},
        {"raise", TOK_RAISE}, {"return", TOK_RETURN}, {"break", TOK_BREAK},
        {"next", TOK_NEXT}, {"yield", TOK_YIELD}, {"self", TOK_SELF},
        {"nil", TOK_NIL}, {"true", TOK_TRUE}, {"false", TOK_FALSE},
        {"and", TOK_AND}, {"or", TOK_OR}, {"not", TOK_NOT},
        {"use", TOK_USE}, {"require", TOK_REQUIRE}, {"require_relative", TOK_REQUIRE},
        {"schema", TOK_SCHEMA}, {"tool", TOK_TOOL},
        {"guard", TOK_GUARD}, {"skill", TOK_SKILL}, {"decide", TOK_DECIDE},
        {"agent", TOK_AGENT}, {"flow", TOK_FLOW}, {"within", TOK_WITHIN},
        {"retry_up_to", TOK_RETRY_UP_TO}, {"mcp_server", TOK_MCP_SERVER},
        {"a2a_agent", TOK_A2A_AGENT},
    };
    for (size_t i = 0; i < sizeof(kws)/sizeof(kws[0]); i++) {
        if (strlen(kws[i].kw) == n && memcmp(kws[i].kw, s, n) == 0) {
            return kws[i].kind;
        }
    }
    return TOK_IDENT;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static char parse_escape(Lexer *lx) {
    char c = advance(lx);
    switch (c) {
        case 'n':  return '\n';
        case 'r':  return '\r';
        case 't':  return '\t';
        case 'e':  return '\x1b';
        case '0':  return '\0';
        case '\\': return '\\';
        case '"':  return '"';
        case '\'': return '\'';
        case '#':  return '#';
        case 'x': {
            int h1 = hex_val(peek(lx, 0));
            int h2 = hex_val(peek(lx, 1));
            if (h1 >= 0 && h2 >= 0) {
                advance(lx);
                advance(lx);
                return (char)((h1 << 4) | h2);
            }
            return 'x';
        }
        default:   return c;
    }
}

/* Strip common leading indentation from a <<~HEREDOC string. */
static char *dedent_heredoc(Arena *a, const char *s, size_t n, size_t *out_n) {
    size_t min_indent = (size_t)-1;
    size_t i = 0;
    while (i < n) {
        size_t ls = i;
        while (i < n && s[i] == ' ') i++;
        if (i < n && s[i] != '\n' && s[i] != '\r') {
            size_t ind = i - ls;
            if (ind < min_indent) min_indent = ind;
        }
        while (i < n && s[i] != '\n') i++;
        if (i < n && s[i] == '\n') i++;
    }
    if (min_indent == (size_t)-1 || min_indent == 0) {
        *out_n = n;
        return arena_strndup(a, s, n);
    }
    StrBuf sb;
    sb_init(&sb);
    i = 0;
    while (i < n) {
        size_t strip = 0;
        while (i < n && s[i] == ' ' && strip < min_indent) { i++; strip++; }
        while (i < n && s[i] != '\n') sb_append_ch(&sb, s[i++]);
        if (i < n && s[i] == '\n') sb_append_ch(&sb, s[i++]);
    }
    char *res = arena_strndup(a, sb.data ? sb.data : "", sb.len);
    *out_n = sb.len;
    sb_free(&sb);
    return res;
}

static void lex_string_segment(Lexer *lx, Span sp, bool continuing) {
    StrBuf sb;
    sb_init(&sb);
    while (lx->pos < lx->len) {
        char c = peek(lx, 0);
        if (c == '"') {
            advance(lx);
            Token t = {0};
            t.kind = continuing ? TOK_STR_TAIL : TOK_STRING;
            t.span = sp;
            t.text = arena_strndup(lx->arena, sb.data ? sb.data : "", sb.len);
            t.len = sb.len;
            sb_free(&sb);
            push_tok(lx, t);
            return;
        }
        if (c == '#' && peek(lx, 1) == '{') {
            advance(lx);
            advance(lx);
            if (lx->interp_depth >= 32) {
                diag_error(lx->diags, cur_span(lx), "string interpolation nested too deeply");
                sb_free(&sb);
                return;
            }
            lx->interp_braces[lx->interp_depth] = 0;
            lx->interp_heredoc[lx->interp_depth] = false;
            lx->interp_depth++;
            Token t = {0};
            t.kind = continuing ? TOK_STR_MID : TOK_STR_HEAD;
            t.span = sp;
            t.text = arena_strndup(lx->arena, sb.data ? sb.data : "", sb.len);
            t.len = sb.len;
            sb_free(&sb);
            push_tok(lx, t);
            return;
        }
        if (c == '\\') {
            advance(lx);
            sb_append_ch(&sb, parse_escape(lx));
        } else {
            sb_append_ch(&sb, advance(lx));
        }
    }
    diag_error(lx->diags, sp, "unterminated string literal");
    sb_free(&sb);
}

static void lex_single_string(Lexer *lx, Span sp) {
    StrBuf sb;
    sb_init(&sb);
    while (lx->pos < lx->len) {
        char c = advance(lx);
        if (c == '\'') {
            Token t = {0};
            t.kind = TOK_STRING;
            t.span = sp;
            t.text = arena_strndup(lx->arena, sb.data ? sb.data : "", sb.len);
            t.len = sb.len;
            sb_free(&sb);
            push_tok(lx, t);
            return;
        }
        if (c == '\\' && (peek(lx, 0) == '\'' || peek(lx, 0) == '\\')) {
            sb_append_ch(&sb, advance(lx));
        } else {
            sb_append_ch(&sb, c);
        }
    }
    diag_error(lx->diags, sp, "unterminated single-quoted string");
    sb_free(&sb);
}

static void lex_heredoc(Lexer *lx, Span sp) {
    /* Already consumed <<~ */
    char tag[64];
    size_t tlen = 0;
    while (is_ident_cont(peek(lx, 0)) && tlen + 1 < sizeof(tag)) {
        tag[tlen++] = advance(lx);
    }
    tag[tlen] = '\0';
    if (tlen == 0) {
        diag_error(lx->diags, sp, "expected heredoc tag after <<~");
        return;
    }
    /* Skip to end of opening line */
    while (lx->pos < lx->len && peek(lx, 0) != '\n') advance(lx);
    if (peek(lx, 0) == '\n') advance(lx);

    StrBuf sb;
    sb_init(&sb);
    bool closed = false;
    while (lx->pos < lx->len) {
        size_t line_start = lx->pos;
        size_t ws = 0;
        while (line_start + ws < lx->len && (lx->src[line_start + ws] == ' ' || lx->src[line_start + ws] == '\t')) ws++;
        if (line_start + ws + tlen <= lx->len &&
            memcmp(lx->src + line_start + ws, tag, tlen) == 0) {
            char after = (line_start + ws + tlen < lx->len) ? lx->src[line_start + ws + tlen] : '\0';
            if (after == '\0' || after == '\n' || after == '\r' || after == ' ' || after == '\t') {
                while (lx->pos < lx->len && peek(lx, 0) != '\n') advance(lx);
                closed = true;
                break;
            }
        }
        while (lx->pos < lx->len) {
            char c = advance(lx);
            sb_append_ch(&sb, c);
            if (c == '\n') break;
        }
    }
    if (!closed) {
        diag_error(lx->diags, sp, "unterminated heredoc <<~%s", tag);
    }
    size_t out_n = 0;
    char *dedented = dedent_heredoc(lx->arena, sb.data ? sb.data : "", sb.len, &out_n);
    sb_free(&sb);
    Token t = {0};
    t.kind = TOK_STRING;
    t.span = sp;
    t.text = dedented;
    t.len = out_n;
    push_tok(lx, t);
}

static void lex_regex(Lexer *lx, Span sp) {
    /* Already consumed opening '/' */
    StrBuf pat;
    sb_init(&pat);
    bool closed = false;
    while (lx->pos < lx->len) {
        char c = advance(lx);
        if (c == '/') { closed = true; break; }
        if (c == '\\' && lx->pos < lx->len) {
            char nxt = advance(lx);
            if (nxt != '/') sb_append_ch(&pat, '\\');
            sb_append_ch(&pat, nxt);
        } else if (c == '\n') {
            break;
        } else {
            sb_append_ch(&pat, c);
        }
    }
    if (!closed) {
        diag_error(lx->diags, sp, "unterminated regex literal");
        sb_free(&pat);
        return;
    }
    StrBuf flags;
    sb_init(&flags);
    while (isalpha((unsigned char)peek(lx, 0))) {
        sb_append_ch(&flags, advance(lx));
    }
    Token t = {0};
    t.kind = TOK_REGEX;
    t.span = sp;
    t.text = arena_strndup(lx->arena, pat.data ? pat.data : "", pat.len);
    t.len = pat.len;
    t.extra = arena_strndup(lx->arena, flags.data ? flags.data : "", flags.len);
    sb_free(&pat);
    sb_free(&flags);
    push_tok(lx, t);
}

static void lex_number(Lexer *lx, Span sp) {
    StrBuf sb;
    sb_init(&sb);
    bool is_float = false;
    bool is_dec = false;

    if (peek(lx, 0) == '0' && (peek(lx, 1) == 'x' || peek(lx, 1) == 'X')) {
        sb_append_ch(&sb, advance(lx));
        sb_append_ch(&sb, advance(lx));
        while (isxdigit((unsigned char)peek(lx, 0)) || peek(lx, 0) == '_') {
            char c = advance(lx);
            if (c != '_') sb_append_ch(&sb, c);
        }
        Token t = {0};
        t.kind = TOK_INT;
        t.span = sp;
        t.text = arena_strndup(lx->arena, sb.data, sb.len);
        t.len = sb.len;
        t.ival = (int64_t)strtoll(sb.data, NULL, 16);
        sb_free(&sb);
        push_tok(lx, t);
        return;
    }

    while (isdigit((unsigned char)peek(lx, 0)) || peek(lx, 0) == '_') {
        char c = advance(lx);
        if (c != '_') sb_append_ch(&sb, c);
    }
    if (peek(lx, 0) == '.' && isdigit((unsigned char)peek(lx, 1))) {
        is_float = true;
        sb_append_ch(&sb, advance(lx));
        while (isdigit((unsigned char)peek(lx, 0)) || peek(lx, 0) == '_') {
            char c = advance(lx);
            if (c != '_') sb_append_ch(&sb, c);
        }
    }
    if ((peek(lx, 0) == 'e' || peek(lx, 0) == 'E') &&
        (isdigit((unsigned char)peek(lx, 1)) ||
         ((peek(lx, 1) == '+' || peek(lx, 1) == '-') && isdigit((unsigned char)peek(lx, 2))))) {
        is_float = true;
        sb_append_ch(&sb, advance(lx));
        if (peek(lx, 0) == '+' || peek(lx, 0) == '-') sb_append_ch(&sb, advance(lx));
        while (isdigit((unsigned char)peek(lx, 0))) sb_append_ch(&sb, advance(lx));
    }
    if (peek(lx, 0) == 'd' && !is_ident_cont(peek(lx, 1))) {
        advance(lx);
        is_dec = true;
    }

    Token t = {0};
    t.span = sp;
    t.text = arena_strndup(lx->arena, sb.data ? sb.data : "0", sb.len);
    t.len = sb.len;
    if (is_dec) {
        t.kind = TOK_DECIMAL;
    } else if (is_float) {
        t.kind = TOK_FLOAT;
        t.fval = strtod(t.text, NULL);
    } else {
        t.kind = TOK_INT;
        t.ival = (int64_t)strtoll(t.text, NULL, 10);
    }
    sb_free(&sb);
    push_tok(lx, t);
}

static void lex_ident_or_label(Lexer *lx, Span sp) {
    size_t start = lx->pos;
    while (is_ident_cont(peek(lx, 0))) advance(lx);
    if ((peek(lx, 0) == '?' || peek(lx, 0) == '!') && peek(lx, 1) != '=') {
        advance(lx);
    }
    size_t n = lx->pos - start;
    const char *s = lx->src + start;

    /* Keyword argument / hash key label `foo:` (not `foo::` and not `a ? b : c`) */
    if (peek(lx, 0) == ':' && peek(lx, 1) != ':') {
        advance(lx);
        Token t = {0};
        t.kind = TOK_LABEL;
        t.span = sp;
        t.text = arena_strndup(lx->arena, s, n);
        t.len = n;
        push_tok(lx, t);
        return;
    }

    Token t = {0};
    t.span = sp;
    t.text = arena_strndup(lx->arena, s, n);
    t.len = n;
    /* After '.' or '::', always treat as identifier (method/const name) */
    if (lx->prev == TOK_DOT || lx->prev == TOK_COLONCOLON) {
        t.kind = TOK_IDENT;
    } else {
        t.kind = lookup_keyword(s, n);
    }
    push_tok(lx, t);
}

void lex_source(const char *file, const char *src, size_t len,
                Arena *arena, DiagList *diags, TokenList *out) {
    memset(out, 0, sizeof(*out));
    Lexer lx = {
        .file = file, .src = src, .len = len, .pos = 0,
        .line = 1, .col = 1, .arena = arena, .diags = diags,
        .out = out, .prev = TOK_NEWLINE, .interp_depth = 0
    };

    while (lx.pos < lx.len) {
        char c = peek(&lx, 0);

        /* Spaces / tabs */
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(&lx);
            continue;
        }

        /* Comments */
        if (c == '#') {
            while (lx.pos < lx.len && peek(&lx, 0) != '\n') advance(&lx);
            continue;
        }

        /* Newline */
        if (c == '\n') {
            Span sp = cur_span(&lx);
            advance(&lx);
            if (lx.prev != TOK_NEWLINE) {
                push_tok(&lx, (Token){ .kind = TOK_NEWLINE, .span = sp, .text = "\n", .len = 1 });
            }
            continue;
        }

        Span sp = cur_span(&lx);

        /* String interpolation closing '}' */
        if (c == '}' && lx.interp_depth > 0 && lx.interp_braces[lx.interp_depth - 1] == 0) {
            advance(&lx);
            lx.interp_depth--;
            lex_string_segment(&lx, sp, true);
            continue;
        }

        /* Strings */
        if (c == '"') {
            advance(&lx);
            lex_string_segment(&lx, sp, false);
            continue;
        }
        if (c == '\'') {
            advance(&lx);
            lex_single_string(&lx, sp);
            continue;
        }

        /* Indented heredoc <<~TAG */
        if (c == '<' && peek(&lx, 1) == '<' && peek(&lx, 2) == '~') {
            advance(&lx); advance(&lx); advance(&lx);
            lex_heredoc(&lx, sp);
            continue;
        }

        /* Numbers */
        if (isdigit((unsigned char)c)) {
            lex_number(&lx, sp);
            continue;
        }

        /* Instance variable @name */
        if (c == '@' && is_ident_start(peek(&lx, 1))) {
            advance(&lx);
            size_t start = lx.pos;
            while (is_ident_cont(peek(&lx, 0))) advance(&lx);
            Token t = {0};
            t.kind = TOK_IVAR;
            t.span = sp;
            t.text = arena_strndup(lx.arena, lx.src + start, lx.pos - start);
            t.len = lx.pos - start;
            push_tok(&lx, t);
            continue;
        }

        /* Symbols :name */
        if (c == ':' && is_ident_start(peek(&lx, 1))) {
            advance(&lx);
            size_t start = lx.pos;
            while (is_ident_cont(peek(&lx, 0))) advance(&lx);
            if ((peek(&lx, 0) == '?' || peek(&lx, 0) == '!') && peek(&lx, 1) != '=') {
                advance(&lx);
            }
            Token t = {0};
            t.kind = TOK_SYMBOL;
            t.span = sp;
            t.text = arena_strndup(lx.arena, lx.src + start, lx.pos - start);
            t.len = lx.pos - start;
            push_tok(&lx, t);
            continue;
        }

        /* Identifiers / keywords / labels */
        if (is_ident_start(c)) {
            lex_ident_or_label(&lx, sp);
            continue;
        }

        /* Regex vs division */
        if (c == '/' && peek(&lx, 1) != '=' && can_start_regex(lx.prev)) {
            advance(&lx);
            lex_regex(&lx, sp);
            continue;
        }

        /* Multi-char operators */
        if (c == '-' && peek(&lx, 1) == '-' && peek(&lx, 2) == '>') {
            advance(&lx); advance(&lx); advance(&lx);
            push_tok(&lx, (Token){ .kind = TOK_FLOW_ARROW, .span = sp, .text = "-->", .len = 3 });
            continue;
        }
        if (c == '.' && peek(&lx, 1) == '.' && peek(&lx, 2) == '.') {
            advance(&lx); advance(&lx); advance(&lx);
            push_tok(&lx, (Token){ .kind = TOK_DOTDOTDOT, .span = sp, .text = "...", .len = 3 });
            continue;
        }
        if (c == '|' && peek(&lx, 1) == '|' && peek(&lx, 2) == '=') {
            advance(&lx); advance(&lx); advance(&lx);
            push_tok(&lx, (Token){ .kind = TOK_OROREQ, .span = sp, .text = "||=", .len = 3 });
            continue;
        }

        char n1 = peek(&lx, 1);
        TokKind two = TOK_EOF;
        if (c == '-' && n1 == '>') two = TOK_ARROW;
        else if (c == '=' && n1 == '>') two = TOK_FAT_ARROW;
        else if (c == '.' && n1 == '.') two = TOK_DOTDOT;
        else if (c == ':' && n1 == ':') two = TOK_COLONCOLON;
        else if (c == '*' && n1 == '*') two = TOK_STARSTAR;
        else if (c == '+' && n1 == '=') two = TOK_PLUSEQ;
        else if (c == '-' && n1 == '=') two = TOK_MINUSEQ;
        else if (c == '*' && n1 == '=') two = TOK_STAREQ;
        else if (c == '/' && n1 == '=') two = TOK_SLASHEQ;
        else if (c == '=' && n1 == '=') two = TOK_EQEQ;
        else if (c == '!' && n1 == '=') two = TOK_BANGEQ;
        else if (c == '<' && n1 == '=') two = TOK_LE;
        else if (c == '>' && n1 == '=') two = TOK_GE;
        else if (c == '=' && n1 == '~') two = TOK_MATCH;
        else if (c == '!' && n1 == '~') two = TOK_NMATCH;
        else if (c == '&' && n1 == '&') two = TOK_ANDAND;
        else if (c == '|' && n1 == '|') two = TOK_OROR;
        else if (c == '<' && n1 == '<') two = TOK_LTLT;
        else if (c == '>' && n1 == '>') two = TOK_GTGT;

        if (two != TOK_EOF) {
            const char *txt = arena_strndup(lx.arena, lx.src + lx.pos, 2);
            advance(&lx); advance(&lx);
            push_tok(&lx, (Token){ .kind = two, .span = sp, .text = txt, .len = 2 });
            continue;
        }

        /* Single-char punctuation & operators */
        advance(&lx);
        TokKind one = TOK_EOF;
        switch (c) {
            case '(': one = TOK_LPAREN; break;
            case ')': one = TOK_RPAREN; break;
            case '[': one = TOK_LBRACK; break;
            case ']': one = TOK_RBRACK; break;
            case '{':
                if (lx.interp_depth > 0) lx.interp_braces[lx.interp_depth - 1]++;
                one = TOK_LBRACE;
                break;
            case '}':
                if (lx.interp_depth > 0) lx.interp_braces[lx.interp_depth - 1]--;
                one = TOK_RBRACE;
                break;
            case ',': one = TOK_COMMA; break;
            case '.': one = TOK_DOT; break;
            case ':': one = TOK_COLON; break;
            case ';': one = TOK_SEMI; break;
            case '?': one = TOK_QMARK; break;
            case '+': one = TOK_PLUS; break;
            case '-': one = TOK_MINUS; break;
            case '*': one = TOK_STAR; break;
            case '/': one = TOK_SLASH; break;
            case '%': one = TOK_PERCENT; break;
            case '=': one = TOK_EQ; break;
            case '<': one = TOK_LT; break;
            case '>': one = TOK_GT; break;
            case '!': one = TOK_BANG; break;
            case '&': one = TOK_AMP; break;
            case '|': one = TOK_PIPE; break;
            case '^': one = TOK_CARET; break;
            default:
                diag_error(lx.diags, sp, "unexpected character '%c'", c);
                continue;
        }
        char buf[2] = { c, '\0' };
        push_tok(&lx, (Token){ .kind = one, .span = sp, .text = arena_strndup(lx.arena, buf, 1), .len = 1 });
    }

    push_tok(&lx, (Token){ .kind = TOK_EOF, .span = cur_span(&lx), .text = "<eof>", .len = 5 });
}

void token_list_free(TokenList *tl) {
    free(tl->items);
    tl->items = NULL;
    tl->len = 0;
    tl->cap = 0;
}

const char *tok_kind_name(TokKind k) {
    switch (k) {
        case TOK_EOF: return "end of file";
        case TOK_NEWLINE: return "newline";
        case TOK_INT: return "integer";
        case TOK_FLOAT: return "float";
        case TOK_DECIMAL: return "decimal";
        case TOK_STRING: return "string";
        case TOK_STR_HEAD: return "interpolated string";
        case TOK_STR_MID: return "string continuation";
        case TOK_STR_TAIL: return "string end";
        case TOK_SYMBOL: return "symbol";
        case TOK_REGEX: return "regex";
        case TOK_IDENT: return "identifier";
        case TOK_LABEL: return "keyword label";
        case TOK_IVAR: return "instance variable";
        case TOK_DEF: return "'def'";
        case TOK_END: return "'end'";
        case TOK_CLASS: return "'class'";
        case TOK_MODULE: return "'module'";
        case TOK_INCLUDE: return "'include'";
        case TOK_IF: return "'if'";
        case TOK_ELSIF: return "'elsif'";
        case TOK_ELSE: return "'else'";
        case TOK_UNLESS: return "'unless'";
        case TOK_CASE: return "'case'";
        case TOK_WHEN: return "'when'";
        case TOK_WHILE: return "'while'";
        case TOK_UNTIL: return "'until'";
        case TOK_DO: return "'do'";
        case TOK_BEGIN: return "'begin'";
        case TOK_RESCUE: return "'rescue'";
        case TOK_ENSURE: return "'ensure'";
        case TOK_RAISE: return "'raise'";
        case TOK_RETURN: return "'return'";
        case TOK_BREAK: return "'break'";
        case TOK_NEXT: return "'next'";
        case TOK_YIELD: return "'yield'";
        case TOK_USE: return "'use'";
        case TOK_REQUIRE: return "'require'";
        case TOK_SCHEMA: return "'schema'";
        case TOK_TOOL: return "'tool'";
        case TOK_GUARD: return "'guard'";
        case TOK_SKILL: return "'skill'";
        case TOK_DECIDE: return "'decide'";
        case TOK_AGENT: return "'agent'";
        case TOK_FLOW: return "'flow'";
        case TOK_WITHIN: return "'within'";
        case TOK_RETRY_UP_TO: return "'retry_up_to'";
        case TOK_MCP_SERVER: return "'mcp_server'";
        case TOK_A2A_AGENT: return "'a2a_agent'";
        case TOK_FLOW_ARROW: return "'-->'";
        case TOK_ARROW: return "'->'";
        case TOK_FAT_ARROW: return "'=>'";
        default: return "token";
    }
}
