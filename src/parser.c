#include "parser.h"

#include <string.h>

typedef struct {
    const TokenList *toks;
    size_t pos;
    Arena *arena;
    DiagList *diags;
    bool no_brace_block; /* true while parsing condition/header where '{' starts a hash, not a block */
    bool no_do_block;    /* true while parsing header of within/retry/while where 'do' belongs to outer stmt */
} Parser;

static const Token *cur(const Parser *p) {
    return &p->toks->items[p->pos < p->toks->len ? p->pos : p->toks->len - 1];
}

static bool at(const Parser *p, TokKind k) {
    return cur(p)->kind == k;
}

static bool at_ident(const Parser *p, const char *name) {
    return cur(p)->kind == TOK_IDENT && strcmp(cur(p)->text, name) == 0;
}

static const Token *bump(Parser *p) {
    const Token *t = cur(p);
    if (p->pos + 1 < p->toks->len) p->pos++;
    return t;
}

static bool eat(Parser *p, TokKind k) {
    if (at(p, k)) { bump(p); return true; }
    return false;
}

static void skip_seps(Parser *p) {
    while (at(p, TOK_NEWLINE) || at(p, TOK_SEMI)) bump(p);
}

static void skip_newlines(Parser *p) {
    while (at(p, TOK_NEWLINE)) bump(p);
}

static bool expect(Parser *p, TokKind k, const char *what) {
    if (at(p, k)) { bump(p); return true; }
    diag_error(p->diags, cur(p)->span, "expected %s, got %s ('%s')",
               what, tok_kind_name(cur(p)->kind), cur(p)->text);
    return false;
}

static const char *expect_name_or_sym(Parser *p, const char *what) {
    if (at(p, TOK_SYMBOL) || at(p, TOK_IDENT)) {
        return bump(p)->text;
    }
    diag_error(p->diags, cur(p)->span, "expected %s, got %s", what, tok_kind_name(cur(p)->kind));
    return "<error>";
}

static bool is_stmt_end(const Parser *p) {
    TokKind k = cur(p)->kind;
    return k == TOK_EOF || k == TOK_NEWLINE || k == TOK_SEMI ||
           k == TOK_END || k == TOK_ELSE || k == TOK_ELSIF ||
           k == TOK_WHEN || k == TOK_RESCUE || k == TOK_ENSURE ||
           k == TOK_RBRACE || k == TOK_RPAREN || k == TOK_RBRACK;
}

/* Forward declarations */
static Node *parse_stmt(Parser *p);
static Node *parse_expr(Parser *p);
static Node *parse_expr_no_brace(Parser *p);
static TypeExpr *parse_type(Parser *p);
static void parse_block_params(Parser *p, ParamList *out);
static void parse_body_until(Parser *p, NodeList *out, TokKind stop1, TokKind stop2, TokKind stop3, TokKind stop4);
static void parse_kv_args(Parser *p, KvList *out);
static Node *parse_tool_decl(Parser *p);
static Node *parse_decide_expr(Parser *p, const char *flow_node_name);

/* ---- Boundary Types ---- */

static TypeExpr *parse_type(Parser *p) {
    Span sp = cur(p)->span;
    if (!at(p, TOK_IDENT)) {
        diag_error(p->diags, sp, "expected type name, got %s", tok_kind_name(cur(p)->kind));
        TypeExpr *err = (TypeExpr *)arena_alloc(p->arena, sizeof(TypeExpr));
        err->kind = TY_NAMED; err->name = "Any"; err->span = sp;
        return err;
    }
    const char *name = bump(p)->text;
    TypeExpr *ty = (TypeExpr *)arena_alloc(p->arena, sizeof(TypeExpr));
    ty->span = sp;
    ty->name = name;

    if (eat(p, TOK_LBRACK)) {
        if (strcmp(name, "Enum") == 0) {
            ty->kind = TY_ENUM;
            const char *vals[64];
            size_t n = 0;
            while (!at(p, TOK_RBRACK) && !at(p, TOK_EOF)) {
                if (n < 64) vals[n++] = expect_name_or_sym(p, "enum symbol");
                else bump(p);
                if (!eat(p, TOK_COMMA)) break;
            }
            expect(p, TOK_RBRACK, "']'");
            ty->enum_vals = (const char **)arena_alloc(p->arena, n * sizeof(const char *));
            if (n) memcpy(ty->enum_vals, vals, n * sizeof(const char *));
            ty->enum_len = n;
        } else {
            ty->kind = TY_GENERIC;
            TypeExpr *args[16];
            size_t n = 0;
            while (!at(p, TOK_RBRACK) && !at(p, TOK_EOF)) {
                if (n < 16) args[n++] = parse_type(p);
                else parse_type(p);
                if (!eat(p, TOK_COMMA)) break;
            }
            expect(p, TOK_RBRACK, "']'");
            ty->args = (TypeExpr **)arena_alloc(p->arena, n * sizeof(TypeExpr *));
            if (n) memcpy(ty->args, args, n * sizeof(TypeExpr *));
            ty->arg_len = n;
        }
    } else {
        ty->kind = TY_NAMED;
    }

    if (eat(p, TOK_QMARK)) {
        TypeExpr *opt = (TypeExpr *)arena_alloc(p->arena, sizeof(TypeExpr));
        opt->kind = TY_OPTIONAL;
        opt->span = sp;
        opt->name = "Optional";
        opt->args = (TypeExpr **)arena_alloc(p->arena, sizeof(TypeExpr *));
        opt->args[0] = ty;
        opt->arg_len = 1;
        return opt;
    }
    return ty;
}

/* ---- Helpers for kv-lists & block params ---- */

static void parse_kv_args(Parser *p, KvList *out) {
    while (at(p, TOK_LABEL)) {
        const Token *lbl = bump(p);
        Node *val = parse_expr_no_brace(p);
        kvlist_push(p->arena, out, lbl->text, val, lbl->span);
        if (!eat(p, TOK_COMMA)) break;
        skip_newlines(p);
    }
}

static void parse_block_params(Parser *p, ParamList *out) {
    if (!eat(p, TOK_PIPE)) return;
    while (!at(p, TOK_PIPE) && !at(p, TOK_EOF)) {
        Span sp = cur(p)->span;
        if (at(p, TOK_IDENT)) {
            const char *nm = bump(p)->text;
            paramlist_push(p->arena, out, (Param){ .name = nm, .span = sp });
        } else {
            diag_error(p->diags, sp, "expected block parameter name");
            break;
        }
        if (!eat(p, TOK_COMMA)) break;
    }
    expect(p, TOK_PIPE, "'|'");
}

static void parse_body_until(Parser *p, NodeList *out, TokKind s1, TokKind s2, TokKind s3, TokKind s4) {
    skip_seps(p);
    while (!at(p, TOK_EOF) && !at(p, s1) && !at(p, s2) && !at(p, s3) && !at(p, s4)) {
        size_t before = p->pos;
        Node *st = parse_stmt(p);
        if (st) nodelist_push(p->arena, out, st);
        if (p->pos == before) bump(p); /* guarantee progress on syntax error */
        skip_seps(p);
    }
}

static void parse_rescue_ensure(Parser *p, RescueClause **out_rescues, size_t *out_rlen, NodeList *out_ensure) {
    RescueClause tmp[16];
    size_t rlen = 0;
    while (eat(p, TOK_RESCUE)) {
        Span sp = cur(p)->span;
        RescueClause rc = { .span = sp };
        if (at(p, TOK_IDENT)) {
            rc.exc_type = bump(p)->text;
        }
        if (eat(p, TOK_FAT_ARROW)) {
            if (at(p, TOK_IDENT)) rc.var_name = bump(p)->text;
            else diag_error(p->diags, cur(p)->span, "expected variable name after '=>'");
        }
        parse_body_until(p, &rc.body, TOK_RESCUE, TOK_ENSURE, TOK_END, TOK_EOF);
        if (rlen < 16) tmp[rlen++] = rc;
    }
    if (rlen > 0) {
        *out_rescues = (RescueClause *)arena_alloc(p->arena, rlen * sizeof(RescueClause));
        memcpy(*out_rescues, tmp, rlen * sizeof(RescueClause));
        *out_rlen = rlen;
    }
    if (eat(p, TOK_ENSURE)) {
        parse_body_until(p, out_ensure, TOK_END, TOK_EOF, TOK_EOF, TOK_EOF);
    }
}

/* ---- Expressions (Pratt / precedence climbing) ---- */

static bool can_start_arg(const Parser *p) {
    TokKind k = cur(p)->kind;
    switch (k) {
        case TOK_INT: case TOK_FLOAT: case TOK_DECIMAL:
        case TOK_STRING: case TOK_STR_HEAD: case TOK_SYMBOL: case TOK_REGEX:
        case TOK_IDENT: case TOK_LABEL: case TOK_IVAR: case TOK_SELF:
        case TOK_NIL: case TOK_TRUE: case TOK_FALSE:
        case TOK_LPAREN: case TOK_LBRACK: case TOK_LBRACE:
        case TOK_ARROW: case TOK_BANG: case TOK_MINUS: case TOK_NOT:
        case TOK_DECIDE: case TOK_WITHIN:
            return true;
        default:
            return false;
    }
}

static void parse_call_args_paren(Parser *p, NodeList *args, KvList *kwargs) {
    /* Already consumed '(' */
    skip_newlines(p);
    while (!at(p, TOK_RPAREN) && !at(p, TOK_EOF)) {
        if (at(p, TOK_LABEL)) {
            const Token *lbl = bump(p);
            Node *val = parse_expr(p);
            kvlist_push(p->arena, kwargs, lbl->text, val, lbl->span);
        } else {
            nodelist_push(p->arena, args, parse_expr(p));
        }
        skip_newlines(p);
        if (!eat(p, TOK_COMMA)) break;
        skip_newlines(p);
    }
    expect(p, TOK_RPAREN, "')'");
}

static void parse_call_args_cmd(Parser *p, NodeList *args, KvList *kwargs) {
    bool saved = p->no_brace_block;
    p->no_brace_block = true;
    while (!is_stmt_end(p) && !at(p, TOK_DO) && can_start_arg(p)) {
        if (at(p, TOK_LABEL)) {
            const Token *lbl = bump(p);
            Node *val = parse_expr(p);
            kvlist_push(p->arena, kwargs, lbl->text, val, lbl->span);
        } else {
            nodelist_push(p->arena, args, parse_expr(p));
        }
        if (!eat(p, TOK_COMMA)) break;
        skip_newlines(p);
    }
    p->no_brace_block = saved;
}

static void maybe_attach_block(Parser *p, Node *call_nd) {
    if (!p->no_do_block && eat(p, TOK_DO)) {
        call_nd->u.call.has_block = true;
        parse_block_params(p, &call_nd->u.call.block_params);
        parse_body_until(p, &call_nd->u.call.block_body, TOK_END, TOK_EOF, TOK_EOF, TOK_EOF);
        expect(p, TOK_END, "'end'");
    } else if (!p->no_brace_block && at(p, TOK_LBRACE)) {
        bump(p);
        call_nd->u.call.has_block = true;
        parse_block_params(p, &call_nd->u.call.block_params);
        parse_body_until(p, &call_nd->u.call.block_body, TOK_RBRACE, TOK_EOF, TOK_EOF, TOK_EOF);
        expect(p, TOK_RBRACE, "'}'");
    }
}

static Node *parse_if_expr(Parser *p, bool invert) {
    Span sp = bump(p)->span; /* if / unless */
    Node *n = node_new(p->arena, ND_IF, sp);
    n->u.if_nd.invert = invert;
    n->u.if_nd.cond = parse_expr_no_brace(p);
    eat(p, TOK_NEWLINE);
    if (at_ident(p, "then")) bump(p);
    parse_body_until(p, &n->u.if_nd.then_body, TOK_ELSIF, TOK_ELSE, TOK_END, TOK_EOF);

    Node *tail = n;
    while (!invert && at(p, TOK_ELSIF)) {
        Span esp = bump(p)->span;
        Node *el = node_new(p->arena, ND_IF, esp);
        el->u.if_nd.cond = parse_expr_no_brace(p);
        eat(p, TOK_NEWLINE);
        if (at_ident(p, "then")) bump(p);
        parse_body_until(p, &el->u.if_nd.then_body, TOK_ELSIF, TOK_ELSE, TOK_END, TOK_EOF);
        nodelist_push(p->arena, &tail->u.if_nd.else_body, el);
        tail = el;
    }
    if (eat(p, TOK_ELSE)) {
        parse_body_until(p, &tail->u.if_nd.else_body, TOK_END, TOK_EOF, TOK_EOF, TOK_EOF);
    }
    expect(p, TOK_END, "'end'");
    return n;
}

static Node *parse_case_expr(Parser *p) {
    Span sp = bump(p)->span; /* case */
    Node *n = node_new(p->arena, ND_CASE, sp);
    if (!at(p, TOK_NEWLINE) && !at(p, TOK_WHEN)) {
        n->u.case_nd.expr = parse_expr_no_brace(p);
    }
    skip_seps(p);
    WhenClause tmp[64];
    size_t wlen = 0;
    while (eat(p, TOK_WHEN)) {
        WhenClause wc = { .span = cur(p)->span };
        do {
            nodelist_push(p->arena, &wc.patterns, parse_expr_no_brace(p));
        } while (eat(p, TOK_COMMA));
        if (at_ident(p, "then")) bump(p);
        parse_body_until(p, &wc.body, TOK_WHEN, TOK_ELSE, TOK_END, TOK_EOF);
        if (wlen < 64) tmp[wlen++] = wc;
    }
    if (wlen > 0) {
        n->u.case_nd.whens = (WhenClause *)arena_alloc(p->arena, wlen * sizeof(WhenClause));
        memcpy(n->u.case_nd.whens, tmp, wlen * sizeof(WhenClause));
        n->u.case_nd.when_len = wlen;
    }
    if (eat(p, TOK_ELSE)) {
        parse_body_until(p, &n->u.case_nd.else_body, TOK_END, TOK_EOF, TOK_EOF, TOK_EOF);
    }
    expect(p, TOK_END, "'end'");
    return n;
}

static Node *parse_begin_expr(Parser *p) {
    Span sp = bump(p)->span; /* begin */
    Node *n = node_new(p->arena, ND_BEGIN, sp);
    parse_body_until(p, &n->u.begin_nd.body, TOK_RESCUE, TOK_ENSURE, TOK_END, TOK_EOF);
    parse_rescue_ensure(p, &n->u.begin_nd.rescues, &n->u.begin_nd.rescue_len, &n->u.begin_nd.ensure_body);
    expect(p, TOK_END, "'end'");
    return n;
}

static Node *parse_within_expr(Parser *p) {
    Span sp = bump(p)->span; /* within */
    Node *n = node_new(p->arena, ND_WITHIN, sp);
    n->u.within_nd.spec = parse_expr_no_brace(p);
    eat(p, TOK_DO);
    parse_body_until(p, &n->u.within_nd.body, TOK_RESCUE, TOK_ENSURE, TOK_END, TOK_EOF);
    parse_rescue_ensure(p, &n->u.within_nd.rescues, &n->u.within_nd.rescue_len, &n->u.within_nd.ensure_body);
    expect(p, TOK_END, "'end'");
    return n;
}

static Node *parse_retry_expr(Parser *p) {
    Span sp = bump(p)->span; /* retry_up_to */
    Node *n = node_new(p->arena, ND_RETRY, sp);
    n->u.retry_nd.count = parse_expr_no_brace(p);
    if (eat(p, TOK_COMMA)) {
        parse_kv_args(p, &n->u.retry_nd.opts);
    }
    eat(p, TOK_DO);
    ParamList bp = {0};
    parse_block_params(p, &bp);
    if (bp.len > 0) n->u.retry_nd.attempt_var = bp.items[0].name;
    parse_body_until(p, &n->u.retry_nd.body, TOK_END, TOK_EOF, TOK_EOF, TOK_EOF);
    expect(p, TOK_END, "'end'");
    return n;
}

static Node *parse_decide_expr(Parser *p, const char *flow_node_name) {
    Span sp = bump(p)->span; /* decide */
    Node *n = node_new(p->arena, ND_DECIDE, sp);
    n->u.decide_nd.node_name = flow_node_name;
    if (!flow_node_name && at(p, TOK_SYMBOL)) {
        n->u.decide_nd.node_name = bump(p)->text;
        eat(p, TOK_COMMA);
    }
    KvList opts = {0};
    parse_kv_args(p, &opts);
    for (size_t i = 0; i < opts.len; i++) {
        if (strcmp(opts.items[i].key, "using") == 0) n->u.decide_nd.using_model = opts.items[i].val;
        else if (strcmp(opts.items[i].key, "on") == 0) n->u.decide_nd.on_expr = opts.items[i].val;
    }
    eat(p, TOK_DO);
    skip_seps(p);
    DecideItem items[64];
    size_t ilen = 0;
    while (!at(p, TOK_END) && !at(p, TOK_EOF)) {
        Span isp = cur(p)->span;
        if (at_ident(p, "choice")) {
            bump(p);
            const char *nm = expect_name_or_sym(p, "choice name");
            eat(p, TOK_COMMA);
            Node *spec = parse_expr(p);
            if (ilen < 64) items[ilen++] = (DecideItem){ .kind = DECIDE_CHOICE, .name = nm, .spec = spec, .span = isp };
        } else if (at_ident(p, "score")) {
            bump(p);
            const char *nm = expect_name_or_sym(p, "score name");
            eat(p, TOK_COMMA);
            Node *spec = parse_expr(p);
            if (ilen < 64) items[ilen++] = (DecideItem){ .kind = DECIDE_SCORE, .name = nm, .spec = spec, .span = isp };
        } else if (at_ident(p, "prob")) {
            bump(p);
            const char *nm = expect_name_or_sym(p, "prob name");
            if (ilen < 64) items[ilen++] = (DecideItem){ .kind = DECIDE_PROB, .name = nm, .spec = NULL, .span = isp };
        } else {
            diag_error(p->diags, isp, "expected 'choice', 'score', or 'prob' inside decide block");
            bump(p);
        }
        skip_seps(p);
    }
    expect(p, TOK_END, "'end'");
    if (ilen > 0) {
        n->u.decide_nd.items = (DecideItem *)arena_alloc(p->arena, ilen * sizeof(DecideItem));
        memcpy(n->u.decide_nd.items, items, ilen * sizeof(DecideItem));
        n->u.decide_nd.item_len = ilen;
    }
    return n;
}

static Node *parse_lambda(Parser *p) {
    Span sp = bump(p)->span; /* -> */
    Node *n = node_new(p->arena, ND_LAMBDA, sp);
    if (eat(p, TOK_LPAREN)) {
        while (!at(p, TOK_RPAREN) && !at(p, TOK_EOF)) {
            Span psp = cur(p)->span;
            const char *nm = expect_name_or_sym(p, "parameter name");
            Node *def = NULL;
            if (eat(p, TOK_EQ)) def = parse_expr(p);
            paramlist_push(p->arena, &n->u.lambda.params, (Param){ .name = nm, .default_val = def, .span = psp });
            if (!eat(p, TOK_COMMA)) break;
        }
        expect(p, TOK_RPAREN, "')'");
    }
    if (eat(p, TOK_LBRACE)) {
        parse_body_until(p, &n->u.lambda.body, TOK_RBRACE, TOK_EOF, TOK_EOF, TOK_EOF);
        expect(p, TOK_RBRACE, "'}'");
    } else if (eat(p, TOK_DO)) {
        parse_body_until(p, &n->u.lambda.body, TOK_END, TOK_EOF, TOK_EOF, TOK_EOF);
        expect(p, TOK_END, "'end'");
    } else {
        diag_error(p->diags, cur(p)->span, "expected '{' or 'do' for lambda body");
    }
    return n;
}

static Node *parse_primary(Parser *p) {
    const Token *t = cur(p);
    Span sp = t->span;

    switch (t->kind) {
        case TOK_NIL:
            bump(p);
            return node_new(p->arena, ND_NIL, sp);
        case TOK_TRUE:
        case TOK_FALSE: {
            bool v = (t->kind == TOK_TRUE);
            bump(p);
            Node *n = node_new(p->arena, ND_BOOL, sp);
            n->u.bval.val = v;
            return n;
        }
        case TOK_SELF:
            bump(p);
            return node_new(p->arena, ND_SELF, sp);
        case TOK_INT: {
            bump(p);
            Node *n = node_new(p->arena, ND_INT, sp);
            n->u.ival.val = t->ival;
            return n;
        }
        case TOK_FLOAT: {
            bump(p);
            Node *n = node_new(p->arena, ND_FLOAT, sp);
            n->u.fval.val = t->fval;
            return n;
        }
        case TOK_DECIMAL: {
            bump(p);
            Node *n = node_new(p->arena, ND_DECIMAL, sp);
            n->u.str.text = t->text;
            n->u.str.len = t->len;
            return n;
        }
        case TOK_STRING: {
            bump(p);
            Node *n = node_new(p->arena, ND_STRING, sp);
            n->u.str.text = t->text;
            n->u.str.len = t->len;
            return n;
        }
        case TOK_STR_HEAD: {
            Node *n = node_new(p->arena, ND_INTERP, sp);
            bump(p);
            if (t->len > 0) {
                Node *s = node_new(p->arena, ND_STRING, sp);
                s->u.str.text = t->text; s->u.str.len = t->len;
                nodelist_push(p->arena, &n->u.interp.parts, s);
            }
            for (;;) {
                nodelist_push(p->arena, &n->u.interp.parts, parse_expr(p));
                const Token *seg = cur(p);
                if (seg->kind == TOK_STR_MID) {
                    bump(p);
                    if (seg->len > 0) {
                        Node *s = node_new(p->arena, ND_STRING, seg->span);
                        s->u.str.text = seg->text; s->u.str.len = seg->len;
                        nodelist_push(p->arena, &n->u.interp.parts, s);
                    }
                } else if (seg->kind == TOK_STR_TAIL) {
                    bump(p);
                    if (seg->len > 0) {
                        Node *s = node_new(p->arena, ND_STRING, seg->span);
                        s->u.str.text = seg->text; s->u.str.len = seg->len;
                        nodelist_push(p->arena, &n->u.interp.parts, s);
                    }
                    break;
                } else {
                    diag_error(p->diags, seg->span, "unterminated string interpolation");
                    break;
                }
            }
            return n;
        }
        case TOK_SYMBOL: {
            bump(p);
            Node *n = node_new(p->arena, ND_SYMBOL, sp);
            n->u.str.text = t->text;
            n->u.str.len = t->len;
            return n;
        }
        case TOK_REGEX: {
            bump(p);
            Node *n = node_new(p->arena, ND_REGEX, sp);
            n->u.regex.pattern = t->text;
            n->u.regex.flags = t->extra ? t->extra : "";
            return n;
        }
        case TOK_IVAR: {
            bump(p);
            Node *n = node_new(p->arena, ND_IVAR, sp);
            n->u.ident.name = t->text;
            return n;
        }
        case TOK_LPAREN: {
            bump(p);
            skip_newlines(p);
            Node *e = parse_expr(p);
            skip_newlines(p);
            expect(p, TOK_RPAREN, "')'");
            return e;
        }
        case TOK_LBRACK: {
            bump(p);
            Node *n = node_new(p->arena, ND_ARRAY, sp);
            skip_newlines(p);
            while (!at(p, TOK_RBRACK) && !at(p, TOK_EOF)) {
                nodelist_push(p->arena, &n->u.array.elems, parse_expr(p));
                skip_newlines(p);
                if (!eat(p, TOK_COMMA)) break;
                skip_newlines(p);
            }
            expect(p, TOK_RBRACK, "']'");
            return n;
        }
        case TOK_LBRACE: {
            bump(p);
            Node *n = node_new(p->arena, ND_HASH, sp);
            skip_newlines(p);
            while (!at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
                if (at(p, TOK_LABEL)) {
                    const Token *lbl = bump(p);
                    Node *k = node_new(p->arena, ND_SYMBOL, lbl->span);
                    k->u.str.text = lbl->text; k->u.str.len = lbl->len;
                    Node *v = parse_expr(p);
                    nodelist_push(p->arena, &n->u.hash.keys, k);
                    nodelist_push(p->arena, &n->u.hash.vals, v);
                } else {
                    Node *k = parse_expr(p);
                    expect(p, TOK_FAT_ARROW, "'=>'");
                    Node *v = parse_expr(p);
                    nodelist_push(p->arena, &n->u.hash.keys, k);
                    nodelist_push(p->arena, &n->u.hash.vals, v);
                }
                skip_newlines(p);
                if (!eat(p, TOK_COMMA)) break;
                skip_newlines(p);
            }
            expect(p, TOK_RBRACE, "'}'");
            return n;
        }
        case TOK_ARROW:
            return parse_lambda(p);
        case TOK_IF:
            return parse_if_expr(p, false);
        case TOK_UNLESS:
            return parse_if_expr(p, true);
        case TOK_CASE:
            return parse_case_expr(p);
        case TOK_BEGIN:
            return parse_begin_expr(p);
        case TOK_WITHIN:
            return parse_within_expr(p);
        case TOK_RETRY_UP_TO:
            return parse_retry_expr(p);
        case TOK_DECIDE:
            return parse_decide_expr(p, NULL);
        case TOK_YIELD: {
            bump(p);
            Node *n = node_new(p->arena, ND_YIELD, sp);
            if (eat(p, TOK_LPAREN)) {
                while (!at(p, TOK_RPAREN) && !at(p, TOK_EOF)) {
                    nodelist_push(p->arena, &n->u.yield_nd.args, parse_expr(p));
                    if (!eat(p, TOK_COMMA)) break;
                }
                expect(p, TOK_RPAREN, "')'");
            } else if (!is_stmt_end(p) && can_start_arg(p)) {
                do {
                    nodelist_push(p->arena, &n->u.yield_nd.args, parse_expr(p));
                } while (eat(p, TOK_COMMA));
            }
            return n;
        }
        case TOK_IDENT: {
            bump(p);
            if (at(p, TOK_LPAREN)) {
                bump(p);
                Node *c = node_new(p->arena, ND_CALL, sp);
                c->u.call.name = t->text;
                parse_call_args_paren(p, &c->u.call.args, &c->u.call.kwargs);
                maybe_attach_block(p, c);
                return c;
            }
            if ((!p->no_do_block && at(p, TOK_DO)) || (!p->no_brace_block && at(p, TOK_LBRACE))) {
                Node *c = node_new(p->arena, ND_CALL, sp);
                c->u.call.name = t->text;
                maybe_attach_block(p, c);
                return c;
            }
            Node *id = node_new(p->arena, ND_IDENT, sp);
            id->u.ident.name = t->text;
            return id;
        }
        default:
            diag_error(p->diags, sp, "unexpected %s ('%s') in expression",
                       tok_kind_name(t->kind), t->text);
            bump(p);
            return node_new(p->arena, ND_NIL, sp);
    }
}

static Node *parse_postfix(Parser *p) {
    Node *lhs = parse_primary(p);
    for (;;) {
        /* Allow fluent chained calls across newlines: foo\n  .map { ... } */
        size_t save = p->pos;
        if (at(p, TOK_NEWLINE)) {
            skip_newlines(p);
            if (!at(p, TOK_DOT)) { p->pos = save; break; }
        }
        if (eat(p, TOK_DOT)) {
            Span sp = cur(p)->span;
            if (!at(p, TOK_IDENT)) {
                diag_error(p->diags, sp, "expected method name after '.'");
                break;
            }
            const char *mname = bump(p)->text;
            Node *c = node_new(p->arena, ND_CALL, sp);
            c->u.call.recv = lhs;
            c->u.call.name = mname;
            if (eat(p, TOK_LPAREN)) {
                parse_call_args_paren(p, &c->u.call.args, &c->u.call.kwargs);
            }
            maybe_attach_block(p, c);
            lhs = c;
        } else if (eat(p, TOK_COLONCOLON)) {
            Span sp = cur(p)->span;
            if (!at(p, TOK_IDENT)) {
                diag_error(p->diags, sp, "expected constant name after '::'");
                break;
            }
            const char *cname = bump(p)->text;
            Node *cp = node_new(p->arena, ND_CONST_PATH, sp);
            cp->u.const_path.base = lhs;
            cp->u.const_path.name = cname;
            lhs = cp;
        } else if (eat(p, TOK_LBRACK)) {
            Span sp = cur(p)->span;
            Node *idx = parse_expr(p);
            expect(p, TOK_RBRACK, "']'");
            Node *ix = node_new(p->arena, ND_INDEX, sp);
            ix->u.idx.obj = lhs;
            ix->u.idx.idx = idx;
            lhs = ix;
        } else {
            break;
        }
    }
    return lhs;
}

static Node *parse_unary(Parser *p) {
    if (at(p, TOK_MINUS) || at(p, TOK_BANG) || at(p, TOK_NOT)) {
        const Token *op = bump(p);
        Node *n = node_new(p->arena, ND_UNARY, op->span);
        n->u.unary.op = op->kind;
        n->u.unary.expr = parse_unary(p);
        return n;
    }
    return parse_postfix(p);
}

static int bin_prec(TokKind k) {
    switch (k) {
        case TOK_OR:
            return 1;
        case TOK_AND:
            return 2;
        case TOK_FLOW_ARROW: /* --> */
            return 3;
        case TOK_OROR:
            return 4;
        case TOK_ANDAND:
            return 5;
        case TOK_EQEQ: case TOK_BANGEQ: case TOK_MATCH: case TOK_NMATCH:
            return 6;
        case TOK_LT: case TOK_LE: case TOK_GT: case TOK_GE:
            return 7;
        case TOK_DOTDOT: case TOK_DOTDOTDOT:
            return 8;
        case TOK_PIPE:
            return 9;
        case TOK_AMP:
            return 10;
        case TOK_PLUS: case TOK_MINUS:
            return 11;
        case TOK_STAR: case TOK_SLASH: case TOK_PERCENT:
            return 12;
        case TOK_STARSTAR:
            return 13;
        default:
            return 0;
    }
}

static Node *parse_binary(Parser *p, int min_prec) {
    Node *lhs = parse_unary(p);
    for (;;) {
        TokKind op = cur(p)->kind;
        int prec = bin_prec(op);
        if (prec < min_prec || prec == 0) break;
        Span sp = bump(p)->span;
        int next_min = (op == TOK_STARSTAR) ? prec : prec + 1;
        Node *rhs = parse_binary(p, next_min);
        if (op == TOK_DOTDOT || op == TOK_DOTDOTDOT) {
            Node *r = node_new(p->arena, ND_RANGE, sp);
            r->u.range.start = lhs;
            r->u.range.stop = rhs;
            r->u.range.excl = (op == TOK_DOTDOTDOT);
            lhs = r;
        } else {
            Node *b = node_new(p->arena, ND_BINARY, sp);
            b->u.binary.op = op;
            b->u.binary.lhs = lhs;
            b->u.binary.rhs = rhs;
            lhs = b;
        }
    }
    return lhs;
}

static bool is_assign_op(TokKind k) {
    return k == TOK_EQ || k == TOK_PLUSEQ || k == TOK_MINUSEQ ||
           k == TOK_STAREQ || k == TOK_SLASHEQ || k == TOK_OROREQ;
}

static Node *parse_expr(Parser *p) {
    Node *lhs = parse_binary(p, 1);

    /* Assignment */
    if (is_assign_op(cur(p)->kind)) {
        const Token *op = bump(p);
        Node *val = parse_expr(p);
        if (lhs->kind == ND_IDENT || lhs->kind == ND_IVAR) {
            Node *a = node_new(p->arena, ND_ASSIGN, op->span);
            a->u.assign.name = lhs->u.ident.name;
            a->u.assign.is_ivar = (lhs->kind == ND_IVAR);
            a->u.assign.op = op->kind;
            a->u.assign.val = val;
            return a;
        }
        if (lhs->kind == ND_INDEX) {
            Node *a = node_new(p->arena, ND_INDEX_ASSIGN, op->span);
            a->u.index_assign.obj = lhs->u.idx.obj;
            a->u.index_assign.idx = lhs->u.idx.idx;
            a->u.index_assign.op = op->kind;
            a->u.index_assign.val = val;
            return a;
        }
        if (lhs->kind == ND_CALL && lhs->u.call.recv && lhs->u.call.args.len == 0 && !lhs->u.call.has_block) {
            Node *a = node_new(p->arena, ND_ATTR_ASSIGN, op->span);
            a->u.attr_assign.obj = lhs->u.call.recv;
            a->u.attr_assign.attr = lhs->u.call.name;
            a->u.attr_assign.op = op->kind;
            a->u.attr_assign.val = val;
            return a;
        }
        diag_error(p->diags, op->span, "invalid assignment target");
        return lhs;
    }

    /* Command-style call without parens: e.g. `puts "hello"`, `obj.add 1, 2` */
    if ((lhs->kind == ND_IDENT || (lhs->kind == ND_CALL && lhs->u.call.args.len == 0 && lhs->u.call.kwargs.len == 0 && !lhs->u.call.has_block)) &&
        !is_stmt_end(p) && !at(p, TOK_DO) && can_start_arg(p)) {
        Node *c;
        if (lhs->kind == ND_IDENT) {
            c = node_new(p->arena, ND_CALL, lhs->span);
            c->u.call.name = lhs->u.ident.name;
        } else {
            c = lhs;
        }
        parse_call_args_cmd(p, &c->u.call.args, &c->u.call.kwargs);
        maybe_attach_block(p, c);
        return c;
    }

    return lhs;
}

static Node *parse_expr_no_brace(Parser *p) {
    bool saved_b = p->no_brace_block;
    bool saved_d = p->no_do_block;
    p->no_brace_block = true;
    p->no_do_block = true;
    Node *e = parse_expr(p);
    p->no_brace_block = saved_b;
    p->no_do_block = saved_d;
    return e;
}

/* ---- Core Ruby Declarations ---- */

static Node *parse_def(Parser *p) {
    Span sp = bump(p)->span; /* def */
    Node *n = node_new(p->arena, ND_DEF, sp);
    if (!at(p, TOK_IDENT)) {
        diag_error(p->diags, cur(p)->span, "expected method name after 'def'");
        n->u.def_nd.name = "<anon>";
    } else {
        n->u.def_nd.name = bump(p)->text;
    }
    if (eat(p, TOK_LPAREN)) {
        while (!at(p, TOK_RPAREN) && !at(p, TOK_EOF)) {
            Span psp = cur(p)->span;
            const char *pname = NULL;
            TypeExpr *pty = NULL;
            Node *pdef = NULL;
            if (at(p, TOK_LABEL)) {
                pname = bump(p)->text;
                pty = parse_type(p);
            } else {
                pname = expect_name_or_sym(p, "parameter name");
            }
            if (eat(p, TOK_EQ)) pdef = parse_expr(p);
            paramlist_push(p->arena, &n->u.def_nd.params, (Param){ .name = pname, .type = pty, .default_val = pdef, .span = psp });
            if (!eat(p, TOK_COMMA)) break;
        }
        expect(p, TOK_RPAREN, "')'");
    }
    if (eat(p, TOK_ARROW)) {
        n->u.def_nd.ret_type = parse_type(p);
    }
    parse_body_until(p, &n->u.def_nd.body, TOK_END, TOK_EOF, TOK_EOF, TOK_EOF);
    expect(p, TOK_END, "'end'");
    return n;
}

static Node *parse_class(Parser *p) {
    Span sp = bump(p)->span; /* class */
    Node *n = node_new(p->arena, ND_CLASS, sp);
    n->u.class_nd.name = expect_name_or_sym(p, "class name");
    if (eat(p, TOK_LT)) {
        n->u.class_nd.super_name = expect_name_or_sym(p, "superclass name");
    }
    parse_body_until(p, &n->u.class_nd.body, TOK_END, TOK_EOF, TOK_EOF, TOK_EOF);
    expect(p, TOK_END, "'end'");
    return n;
}

static Node *parse_module(Parser *p) {
    Span sp = bump(p)->span; /* module */
    Node *n = node_new(p->arena, ND_MODULE, sp);
    n->u.mod_nd.name = expect_name_or_sym(p, "module name");
    parse_body_until(p, &n->u.mod_nd.body, TOK_END, TOK_EOF, TOK_EOF, TOK_EOF);
    expect(p, TOK_END, "'end'");
    return n;
}

/* ---- First-Class AIL Declarations ---- */

static Node *parse_use_decl(Parser *p) {
    Span sp = bump(p)->span; /* use */
    Node *n = node_new(p->arena, ND_USE, sp);
    if (at(p, TOK_LABEL)) {
        n->u.use_nd.kind = bump(p)->text;
        n->u.use_nd.name = expect_name_or_sym(p, "binding name");
        if (eat(p, TOK_COMMA)) {
            parse_kv_args(p, &n->u.use_nd.opts);
        }
    } else {
        diag_error(p->diags, cur(p)->span, "expected 'use kind: :name' (e.g. use llm: :reasoning)");
    }
    return n;
}

static FieldDecl parse_field_or_param(Parser *p) {
    Span sp = bump(p)->span; /* field / param */
    FieldDecl f = { .span = sp };
    f.name = expect_name_or_sym(p, "field name");
    expect(p, TOK_COMMA, "','");
    f.type = parse_type(p);
    if (eat(p, TOK_EQ)) {
        f.default_val = parse_expr(p);
    }
    if (eat(p, TOK_COMMA)) {
        if (at(p, TOK_STRING)) {
            f.doc = bump(p)->text;
        } else if (at(p, TOK_LABEL)) {
            KvList kv = {0};
            parse_kv_args(p, &kv);
            for (size_t i = 0; i < kv.len; i++) {
                if (strcmp(kv.items[i].key, "default") == 0) f.default_val = kv.items[i].val;
                else if (strcmp(kv.items[i].key, "doc") == 0 && kv.items[i].val->kind == ND_STRING)
                    f.doc = kv.items[i].val->u.str.text;
            }
        }
    }
    return f;
}

static Node *parse_schema_decl(Parser *p) {
    Span sp = bump(p)->span; /* schema */
    Node *n = node_new(p->arena, ND_SCHEMA, sp);
    n->u.schema_nd.name = expect_name_or_sym(p, "schema name");
    eat(p, TOK_DO);
    skip_seps(p);
    FieldDecl fields[128];
    size_t flen = 0;
    while (!at(p, TOK_END) && !at(p, TOK_EOF)) {
        if (at_ident(p, "field")) {
            FieldDecl f = parse_field_or_param(p);
            if (flen < 128) fields[flen++] = f;
        } else {
            diag_error(p->diags, cur(p)->span, "expected 'field' inside schema %s", n->u.schema_nd.name);
            bump(p);
        }
        skip_seps(p);
    }
    expect(p, TOK_END, "'end'");
    if (flen > 0) {
        n->u.schema_nd.fields = (FieldDecl *)arena_alloc(p->arena, flen * sizeof(FieldDecl));
        memcpy(n->u.schema_nd.fields, fields, flen * sizeof(FieldDecl));
        n->u.schema_nd.field_len = flen;
    }
    return n;
}

static Node *parse_tool_decl(Parser *p) {
    Span sp = bump(p)->span; /* tool */
    Node *n = node_new(p->arena, ND_TOOL, sp);
    n->u.tool_nd.name = expect_name_or_sym(p, "tool name");
    if (eat(p, TOK_COMMA) && at(p, TOK_STRING)) {
        n->u.tool_nd.doc = bump(p)->text;
    }
    eat(p, TOK_DO);
    skip_seps(p);

    FieldDecl params[64];
    size_t plen = 0;
    while (!at(p, TOK_END) && !at(p, TOK_EOF)) {
        if (at_ident(p, "param")) {
            FieldDecl f = parse_field_or_param(p);
            if (plen < 64) params[plen++] = f;
        } else if (at_ident(p, "returns")) {
            bump(p);
            n->u.tool_nd.ret_type = parse_type(p);
        } else if (at_ident(p, "capabilities")) {
            bump(p);
            parse_kv_args(p, &n->u.tool_nd.capabilities);
        } else if (at_ident(p, "limits")) {
            bump(p);
            parse_kv_args(p, &n->u.tool_nd.limits);
        } else {
            size_t before = p->pos;
            Node *st = parse_stmt(p);
            if (st) nodelist_push(p->arena, &n->u.tool_nd.body, st);
            if (p->pos == before) bump(p);
        }
        skip_seps(p);
    }
    expect(p, TOK_END, "'end'");
    if (plen > 0) {
        n->u.tool_nd.params = (FieldDecl *)arena_alloc(p->arena, plen * sizeof(FieldDecl));
        memcpy(n->u.tool_nd.params, params, plen * sizeof(FieldDecl));
        n->u.tool_nd.param_len = plen;
    }
    return n;
}

static Node *parse_guard_decl(Parser *p) {
    Span sp = bump(p)->span; /* guard */
    Node *n = node_new(p->arena, ND_GUARD, sp);
    n->u.guard_nd.name = expect_name_or_sym(p, "guard name");
    eat(p, TOK_DO);
    skip_seps(p);

    GuardHook hooks[16];
    size_t hlen = 0;
    while (!at(p, TOK_END) && !at(p, TOK_EOF)) {
        Span hsp = cur(p)->span;
        if (at_ident(p, "check_input") || at_ident(p, "check_output") || at_ident(p, "check_tool")) {
            const char *hk = bump(p)->text;
            GuardHook h = { .span = hsp };
            if (strcmp(hk, "check_input") == 0) h.kind = GUARD_INPUT;
            else if (strcmp(hk, "check_output") == 0) h.kind = GUARD_OUTPUT;
            else h.kind = GUARD_TOOL;
            expect(p, TOK_DO, "'do'");
            parse_block_params(p, &h.params);
            parse_body_until(p, &h.body, TOK_END, TOK_EOF, TOK_EOF, TOK_EOF);
            expect(p, TOK_END, "'end'");
            if (hlen < 16) hooks[hlen++] = h;
        } else if (at_ident(p, "capabilities")) {
            bump(p);
            parse_kv_args(p, &n->u.guard_nd.capabilities);
        } else {
            diag_error(p->diags, hsp, "expected 'check_input', 'check_output', 'check_tool', or 'capabilities' inside guard %s", n->u.guard_nd.name);
            bump(p);
        }
        skip_seps(p);
    }
    expect(p, TOK_END, "'end'");
    if (hlen > 0) {
        n->u.guard_nd.hooks = (GuardHook *)arena_alloc(p->arena, hlen * sizeof(GuardHook));
        memcpy(n->u.guard_nd.hooks, hooks, hlen * sizeof(GuardHook));
        n->u.guard_nd.hook_len = hlen;
    }
    return n;
}

static Node *parse_skill_decl(Parser *p) {
    Span sp = bump(p)->span; /* skill */
    Node *n = node_new(p->arena, ND_SKILL, sp);
    n->u.skill_nd.name = expect_name_or_sym(p, "skill name");
    if (eat(p, TOK_COMMA) && at(p, TOK_STRING)) {
        n->u.skill_nd.doc = bump(p)->text;
    }
    eat(p, TOK_DO);
    skip_seps(p);
    while (!at(p, TOK_END) && !at(p, TOK_EOF)) {
        if (at_ident(p, "tools")) {
            bump(p);
            n->u.skill_nd.tools = parse_expr(p);
        } else if (at_ident(p, "capabilities")) {
            bump(p);
            parse_kv_args(p, &n->u.skill_nd.capabilities);
        } else if (at_ident(p, "instructions")) {
            bump(p);
            n->u.skill_nd.instructions = parse_expr(p);
        } else {
            size_t before = p->pos;
            Node *st = parse_stmt(p);
            if (st) nodelist_push(p->arena, &n->u.skill_nd.body, st);
            if (p->pos == before) bump(p);
        }
        skip_seps(p);
    }
    expect(p, TOK_END, "'end'");
    return n;
}

static Node *parse_agent_decl(Parser *p) {
    Span sp = bump(p)->span; /* agent */
    Node *n = node_new(p->arena, ND_AGENT, sp);
    n->u.agent_nd.name = expect_name_or_sym(p, "agent name");
    eat(p, TOK_DO);
    skip_seps(p);
    while (!at(p, TOK_END) && !at(p, TOK_EOF)) {
        if (at_ident(p, "model")) { bump(p); n->u.agent_nd.model = parse_expr(p); }
        else if (at_ident(p, "context_view")) { bump(p); n->u.agent_nd.context_view = parse_expr(p); }
        else if (at_ident(p, "tools")) { bump(p); n->u.agent_nd.tools = parse_expr(p); }
        else if (at_ident(p, "mcp")) { bump(p); n->u.agent_nd.mcp = parse_expr(p); }
        else if (at_ident(p, "skills")) { bump(p); n->u.agent_nd.skills = parse_expr(p); }
        else if (at_ident(p, "guards")) { bump(p); n->u.agent_nd.guards = parse_expr(p); }
        else if (at_ident(p, "system")) { bump(p); n->u.agent_nd.system_prompt = parse_expr(p); }
        else if (at_ident(p, "returns")) { bump(p); n->u.agent_nd.ret_type = parse_type(p); }
        else if (at_ident(p, "capabilities")) { bump(p); parse_kv_args(p, &n->u.agent_nd.capabilities); }
        else if (at_ident(p, "limits")) { bump(p); parse_kv_args(p, &n->u.agent_nd.limits); }
        else {
            diag_error(p->diags, cur(p)->span, "unexpected '%s' inside agent %s", cur(p)->text, n->u.agent_nd.name);
            bump(p);
        }
        skip_seps(p);
    }
    expect(p, TOK_END, "'end'");
    return n;
}

static Node *parse_flow_decl(Parser *p) {
    Span sp = bump(p)->span; /* flow */
    Node *n = node_new(p->arena, ND_FLOW, sp);
    n->u.flow_nd.name = expect_name_or_sym(p, "flow name");
    eat(p, TOK_DO);
    skip_seps(p);

    FlowNodeDecl nodes[128];
    size_t nlen = 0;
    FlowEdgeDecl edges[256];
    size_t elen = 0;

    while (!at(p, TOK_END) && !at(p, TOK_EOF)) {
        Span isp = cur(p)->span;
        if (at_ident(p, "state")) {
            bump(p);
            n->u.flow_nd.state_type = parse_type(p);
        } else if (at_ident(p, "start")) {
            bump(p);
            n->u.flow_nd.start_node = expect_name_or_sym(p, "start node name");
        } else if (at_ident(p, "node")) {
            bump(p);
            const char *from = expect_name_or_sym(p, "node name");
            if (nlen < 128) nodes[nlen++] = (FlowNodeDecl){ .name = from, .handler = NULL, .span = isp };
            if (eat(p, TOK_FLOW_ARROW)) {
                const char *to = expect_name_or_sym(p, "target node name");
                if (elen < 256) edges[elen++] = (FlowEdgeDecl){ .from = from, .to = to, .span = isp };
            } else if (eat(p, TOK_COMMA)) {
                nodes[nlen - 1].handler = parse_expr(p);
            }
        } else if (at(p, TOK_DECIDE)) {
            Node *dec = parse_decide_expr(p, NULL);
            const char *dname = dec->u.decide_nd.node_name ? dec->u.decide_nd.node_name : "decide";
            if (nlen < 128) nodes[nlen++] = (FlowNodeDecl){ .name = dname, .handler = dec, .span = isp };
        } else if (at_ident(p, "edge")) {
            bump(p);
            const char *from = expect_name_or_sym(p, "source node");
            expect(p, TOK_FLOW_ARROW, "'-->'");
            const char *to = expect_name_or_sym(p, "target node");
            FlowEdgeDecl e = { .from = from, .to = to, .span = isp };
            if (eat(p, TOK_COMMA)) {
                KvList kv = {0};
                parse_kv_args(p, &kv);
                for (size_t i = 0; i < kv.len; i++) {
                    if (strcmp(kv.items[i].key, "when") == 0) { e.cond = kv.items[i].val; e.invert_cond = false; }
                    else if (strcmp(kv.items[i].key, "unless") == 0) { e.cond = kv.items[i].val; e.invert_cond = true; }
                    else if (strcmp(kv.items[i].key, "max") == 0) { e.max_bound = kv.items[i].val; }
                }
            }
            if (elen < 256) edges[elen++] = e;
        } else {
            diag_error(p->diags, isp, "unexpected '%s' inside flow %s", cur(p)->text, n->u.flow_nd.name);
            bump(p);
        }
        skip_seps(p);
    }
    expect(p, TOK_END, "'end'");
    if (nlen > 0) {
        n->u.flow_nd.nodes = (FlowNodeDecl *)arena_alloc(p->arena, nlen * sizeof(FlowNodeDecl));
        memcpy(n->u.flow_nd.nodes, nodes, nlen * sizeof(FlowNodeDecl));
        n->u.flow_nd.node_len = nlen;
    }
    if (elen > 0) {
        n->u.flow_nd.edges = (FlowEdgeDecl *)arena_alloc(p->arena, elen * sizeof(FlowEdgeDecl));
        memcpy(n->u.flow_nd.edges, edges, elen * sizeof(FlowEdgeDecl));
        n->u.flow_nd.edge_len = elen;
    }
    return n;
}

static Node *parse_mcp_server(Parser *p) {
    Span sp = bump(p)->span; /* mcp_server */
    Node *n = node_new(p->arena, ND_MCP_SERVER, sp);
    if (at(p, TOK_STRING)) n->u.mcp_nd.name = bump(p)->text;
    else { diag_error(p->diags, cur(p)->span, "expected MCP server name string"); n->u.mcp_nd.name = "mcp"; }
    n->u.mcp_nd.version = "0.1.0";
    if (eat(p, TOK_COMMA)) {
        KvList kv = {0};
        parse_kv_args(p, &kv);
        for (size_t i = 0; i < kv.len; i++) {
            if (strcmp(kv.items[i].key, "version") == 0 && kv.items[i].val->kind == ND_STRING) {
                n->u.mcp_nd.version = kv.items[i].val->u.str.text;
            }
        }
    }
    eat(p, TOK_DO);
    skip_seps(p);

    const char *exposed[128];
    size_t elen = 0;
    McpResourceDecl res[64];
    size_t rlen = 0;
    McpPromptDecl prompts[64];
    size_t prlen = 0;

    while (!at(p, TOK_END) && !at(p, TOK_EOF)) {
        Span isp = cur(p)->span;
        if (at_ident(p, "capabilities")) {
            bump(p);
            parse_kv_args(p, &n->u.mcp_nd.capabilities);
        } else if (at_ident(p, "expose")) {
            bump(p);
            do {
                const char *tn = expect_name_or_sym(p, "tool name");
                if (elen < 128) exposed[elen++] = tn;
            } while (eat(p, TOK_COMMA));
        } else if (at(p, TOK_TOOL)) {
            nodelist_push(p->arena, &n->u.mcp_nd.inline_tools, parse_tool_decl(p));
        } else if (at_ident(p, "resource")) {
            bump(p);
            McpResourceDecl rd = { .span = isp, .mime = "text/plain" };
            if (at(p, TOK_STRING)) rd.uri = bump(p)->text;
            else { diag_error(p->diags, cur(p)->span, "expected resource URI string"); rd.uri = ""; }
            if (eat(p, TOK_COMMA)) {
                KvList kv = {0};
                parse_kv_args(p, &kv);
                for (size_t i = 0; i < kv.len; i++) {
                    if (strcmp(kv.items[i].key, "mime") == 0 && kv.items[i].val->kind == ND_STRING)
                        rd.mime = kv.items[i].val->u.str.text;
                }
            }
            eat(p, TOK_DO);
            parse_body_until(p, &rd.body, TOK_END, TOK_EOF, TOK_EOF, TOK_EOF);
            expect(p, TOK_END, "'end'");
            if (rlen < 64) res[rlen++] = rd;
        } else if (at_ident(p, "prompt")) {
            bump(p);
            McpPromptDecl pd = { .span = isp };
            pd.name = expect_name_or_sym(p, "prompt name");
            if (eat(p, TOK_COMMA) && at(p, TOK_STRING)) pd.doc = bump(p)->text;
            eat(p, TOK_DO);
            skip_seps(p);
            FieldDecl ftmp[32];
            size_t flen = 0;
            while (!at(p, TOK_END) && !at(p, TOK_EOF)) {
                if (at_ident(p, "param")) {
                    FieldDecl f = parse_field_or_param(p);
                    if (flen < 32) ftmp[flen++] = f;
                } else {
                    Node *st = parse_stmt(p);
                    if (st) nodelist_push(p->arena, &pd.body, st);
                }
                skip_seps(p);
            }
            expect(p, TOK_END, "'end'");
            if (flen > 0) {
                pd.params = (FieldDecl *)arena_alloc(p->arena, flen * sizeof(FieldDecl));
                memcpy(pd.params, ftmp, flen * sizeof(FieldDecl));
                pd.param_len = flen;
            }
            if (prlen < 64) prompts[prlen++] = pd;
        } else {
            diag_error(p->diags, isp, "unexpected '%s' inside mcp_server", cur(p)->text);
            bump(p);
        }
        skip_seps(p);
    }
    expect(p, TOK_END, "'end'");
    if (elen > 0) {
        n->u.mcp_nd.exposed = (const char **)arena_alloc(p->arena, elen * sizeof(const char *));
        memcpy(n->u.mcp_nd.exposed, exposed, elen * sizeof(const char *));
        n->u.mcp_nd.exposed_len = elen;
    }
    if (rlen > 0) {
        n->u.mcp_nd.resources = (McpResourceDecl *)arena_alloc(p->arena, rlen * sizeof(McpResourceDecl));
        memcpy(n->u.mcp_nd.resources, res, rlen * sizeof(McpResourceDecl));
        n->u.mcp_nd.resource_len = rlen;
    }
    if (prlen > 0) {
        n->u.mcp_nd.prompts = (McpPromptDecl *)arena_alloc(p->arena, prlen * sizeof(McpPromptDecl));
        memcpy(n->u.mcp_nd.prompts, prompts, prlen * sizeof(McpPromptDecl));
        n->u.mcp_nd.prompt_len = prlen;
    }
    return n;
}

static Node *parse_a2a_agent(Parser *p) {
    Span sp = bump(p)->span; /* a2a_agent */
    Node *n = node_new(p->arena, ND_A2A_AGENT, sp);
    if (at(p, TOK_STRING)) n->u.a2a_nd.name = bump(p)->text;
    else { diag_error(p->diags, cur(p)->span, "expected a2a_agent name string"); n->u.a2a_nd.name = "a2a"; }
    n->u.a2a_nd.version = "0.1.0";
    if (eat(p, TOK_COMMA)) {
        KvList kv = {0};
        parse_kv_args(p, &kv);
        for (size_t i = 0; i < kv.len; i++) {
            if (strcmp(kv.items[i].key, "version") == 0 && kv.items[i].val->kind == ND_STRING)
                n->u.a2a_nd.version = kv.items[i].val->u.str.text;
        }
    }
    eat(p, TOK_DO);
    skip_seps(p);
    while (!at(p, TOK_END) && !at(p, TOK_EOF)) {
        if (at_ident(p, "description")) { bump(p); n->u.a2a_nd.description = parse_expr(p); }
        else if (at_ident(p, "handler")) { bump(p); n->u.a2a_nd.handler = parse_expr(p); }
        else if (at_ident(p, "capabilities")) { bump(p); parse_kv_args(p, &n->u.a2a_nd.capabilities); }
        else if (at_ident(p, "limits")) { bump(p); parse_kv_args(p, &n->u.a2a_nd.limits); }
        else {
            diag_error(p->diags, cur(p)->span, "unexpected '%s' inside a2a_agent", cur(p)->text);
            bump(p);
        }
        skip_seps(p);
    }
    expect(p, TOK_END, "'end'");
    return n;
}

/* ---- Statements ---- */

static Node *parse_stmt(Parser *p) {
    Span sp = cur(p)->span;
    Node *st = NULL;

    switch (cur(p)->kind) {
        case TOK_REQUIRE: {
            bump(p);
            bool has_p = eat(p, TOK_LPAREN);
            st = node_new(p->arena, ND_REQUIRE, sp);
            if (at(p, TOK_STRING)) {
                st->u.str.text = bump(p)->text;
                st->u.str.len = strlen(st->u.str.text);
            } else {
                diag_error(p->diags, cur(p)->span, "expected file path string after 'require'");
                st->u.str.text = "";
            }
            if (has_p) expect(p, TOK_RPAREN, "')'");
            return st;
        }
        case TOK_USE:        return parse_use_decl(p);
        case TOK_SCHEMA:     return parse_schema_decl(p);
        case TOK_TOOL:       return parse_tool_decl(p);
        case TOK_GUARD:      return parse_guard_decl(p);
        case TOK_SKILL:      return parse_skill_decl(p);
        case TOK_AGENT:      return parse_agent_decl(p);
        case TOK_FLOW:       return parse_flow_decl(p);
        case TOK_MCP_SERVER: return parse_mcp_server(p);
        case TOK_A2A_AGENT:  return parse_a2a_agent(p);
        case TOK_DEF:        return parse_def(p);
        case TOK_CLASS:      return parse_class(p);
        case TOK_MODULE:     return parse_module(p);
        case TOK_INCLUDE: {
            bump(p);
            st = node_new(p->arena, ND_INCLUDE, sp);
            st->u.inc_nd.name = expect_name_or_sym(p, "module name");
            return st;
        }
        case TOK_WHILE:
        case TOK_UNTIL: {
            bool inv = (cur(p)->kind == TOK_UNTIL);
            bump(p);
            st = node_new(p->arena, ND_WHILE, sp);
            st->u.while_nd.invert = inv;
            st->u.while_nd.cond = parse_expr_no_brace(p);
            eat(p, TOK_DO);
            parse_body_until(p, &st->u.while_nd.body, TOK_END, TOK_EOF, TOK_EOF, TOK_EOF);
            expect(p, TOK_END, "'end'");
            return st;
        }
        case TOK_RETURN: {
            bump(p);
            st = node_new(p->arena, ND_RETURN, sp);
            if (!is_stmt_end(p) && !at(p, TOK_IF) && !at(p, TOK_UNLESS)) {
                st->u.ret.val = parse_expr(p);
            }
            break;
        }
        case TOK_BREAK:
            bump(p);
            st = node_new(p->arena, ND_BREAK, sp);
            break;
        case TOK_NEXT:
            bump(p);
            st = node_new(p->arena, ND_NEXT, sp);
            break;
        case TOK_RAISE: {
            bump(p);
            st = node_new(p->arena, ND_RAISE, sp);
            if (!is_stmt_end(p) && !at(p, TOK_IF) && !at(p, TOK_UNLESS)) {
                Node *first = parse_expr(p);
                if (eat(p, TOK_COMMA)) {
                    st->u.raise_nd.exc = first;
                    st->u.raise_nd.msg = parse_expr(p);
                } else {
                    st->u.raise_nd.msg = first;
                }
            }
            break;
        }
        default:
            st = parse_expr(p);
            break;
    }

    /* Trailing statement modifier: `stmt if cond` / `stmt unless cond` */
    if (st && (at(p, TOK_IF) || at(p, TOK_UNLESS))) {
        bool inv = (cur(p)->kind == TOK_UNLESS);
        Span msp = bump(p)->span;
        Node *wrap = node_new(p->arena, ND_IF, msp);
        wrap->u.if_nd.invert = inv;
        wrap->u.if_nd.cond = parse_expr(p);
        nodelist_push(p->arena, &wrap->u.if_nd.then_body, st);
        st = wrap;
    }
    return st;
}

Node *parse_program(const TokenList *tokens, Arena *arena, DiagList *diags) {
    Parser p = { .toks = tokens, .pos = 0, .arena = arena, .diags = diags };
    Node *prog = node_new(arena, ND_PROGRAM, cur(&p)->span);
    parse_body_until(&p, &prog->u.block.stmts, TOK_EOF, TOK_EOF, TOK_EOF, TOK_EOF);
    return prog;
}
