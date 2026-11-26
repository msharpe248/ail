#ifndef AIL_LEXER_H
#define AIL_LEXER_H

#include "util.h"

typedef enum {
    TOK_EOF = 0,
    TOK_NEWLINE,

    /* Literals & names */
    TOK_INT,
    TOK_FLOAT,
    TOK_DECIMAL,     /* 0.25d */
    TOK_STRING,      /* plain string */
    TOK_STR_HEAD,    /* "prefix #{ */
    TOK_STR_MID,     /* } mid #{ */
    TOK_STR_TAIL,    /* } suffix" */
    TOK_SYMBOL,      /* :name */
    TOK_REGEX,       /* /pattern/flags */
    TOK_IDENT,       /* name, name?, name! */
    TOK_LABEL,       /* name: (keyword arg / hash key) */
    TOK_IVAR,        /* @name */

    /* Core Ruby keywords */
    TOK_DEF,
    TOK_END,
    TOK_CLASS,
    TOK_MODULE,
    TOK_INCLUDE,
    TOK_IF,
    TOK_ELSIF,
    TOK_ELSE,
    TOK_UNLESS,
    TOK_CASE,
    TOK_WHEN,
    TOK_WHILE,
    TOK_UNTIL,
    TOK_FOR,
    TOK_IN,
    TOK_DO,
    TOK_BEGIN,
    TOK_RESCUE,
    TOK_ENSURE,
    TOK_RAISE,
    TOK_RETURN,
    TOK_BREAK,
    TOK_NEXT,
    TOK_YIELD,
    TOK_SELF,
    TOK_NIL,
    TOK_TRUE,
    TOK_FALSE,
    TOK_AND,
    TOK_OR,
    TOK_NOT,

    /* Top-level / statement-leading AIL keywords */
    TOK_REQUIRE,
    TOK_USE,
    TOK_SCHEMA,
    TOK_TOOL,
    TOK_GUARD,
    TOK_SKILL,
    TOK_DECIDE,
    TOK_AGENT,
    TOK_FLOW,
    TOK_WITHIN,
    TOK_RETRY_UP_TO,
    TOK_MCP_SERVER,
    TOK_A2A_AGENT,

    /* Operators & delimiters */
    TOK_LPAREN,      /* ( */
    TOK_RPAREN,      /* ) */
    TOK_LBRACK,      /* [ */
    TOK_RBRACK,      /* ] */
    TOK_LBRACE,      /* { */
    TOK_RBRACE,      /* } */
    TOK_COMMA,       /* , */
    TOK_DOT,         /* . */
    TOK_DOTDOT,      /* .. */
    TOK_DOTDOTDOT,   /* ... */
    TOK_COLON,       /* : */
    TOK_COLONCOLON,  /* :: */
    TOK_SEMI,        /* ; */
    TOK_QMARK,       /* ? */

    TOK_FLOW_ARROW,  /* --> */
    TOK_ARROW,       /* -> */
    TOK_FAT_ARROW,   /* => */

    TOK_PLUS,        /* + */
    TOK_MINUS,       /* - */
    TOK_STAR,        /* * */
    TOK_STARSTAR,    /* ** */
    TOK_SLASH,       /* / */
    TOK_PERCENT,     /* % */

    TOK_EQ,          /* = */
    TOK_PLUSEQ,      /* += */
    TOK_MINUSEQ,     /* -= */
    TOK_STAREQ,      /* *= */
    TOK_SLASHEQ,     /* /= */
    TOK_OROREQ,      /* ||= */

    TOK_EQEQ,        /* == */
    TOK_BANGEQ,      /* != */
    TOK_LT,          /* < */
    TOK_LE,          /* <= */
    TOK_GT,          /* > */
    TOK_GE,          /* >= */
    TOK_MATCH,       /* =~ */
    TOK_NMATCH,      /* !~ */

    TOK_ANDAND,      /* && */
    TOK_OROR,        /* || */
    TOK_BANG,        /* ! */
    TOK_AMP,         /* & */
    TOK_PIPE,        /* | */
    TOK_CARET,       /* ^ */
    TOK_LTLT,        /* << */
    TOK_GTGT         /* >> */
} TokKind;

typedef struct {
    TokKind kind;
    Span span;
    const char *text;   /* arena-allocated decoded text for strings/idents/symbols/decimals/regex */
    size_t len;
    const char *extra;  /* regex flags */
    int64_t ival;
    double fval;
} Token;

typedef struct {
    Token *items;
    size_t len;
    size_t cap;
} TokenList;

void lex_source(const char *file, const char *src, size_t len,
                Arena *arena, DiagList *diags, TokenList *out);
void token_list_free(TokenList *tl);
const char *tok_kind_name(TokKind k);

#endif
