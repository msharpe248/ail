#include "codegen.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *source_file;
    const SemaInfo *sema;
    Arena *arena;
    DiagList *diags;
    StrBuf funcs;      /* emitted helper / user / closure functions */
    StrBuf *cur;       /* active output buffer */
    int lbl_seq;
    int fn_seq;

    /* Active loop labels for break/next */
    int loop_depth;
    int break_lbl[32];
    int next_lbl[32];
} Codegen;

static int next_lbl(Codegen *cg) { return ++cg->lbl_seq; }
static int next_fn(Codegen *cg)  { return ++cg->fn_seq; }

static void emit_loc(Codegen *cg, Span sp) {
    const char *f = sp.file ? sp.file : cg->source_file;
    if (sp.line > 0 && f) {
        sb_append(cg->cur, "    .loc ");
        sb_append_escaped(cg->cur, f, strlen(f));
        sb_printf(cg->cur, ", %d\n", sp.line);
    }
}

/* Convention inside every emitted AIL function:
   Parameters: (ref self, ref args, ref block) -> ref
   Stack locals:
     LOC[0] = env map
     LOC[1] = self
     LOC[2] = block
   Expression result always lands in R3!
*/

static void emit_expr(Codegen *cg, Node *n);
static void emit_stmts(Codegen *cg, const NodeList *nl);

/* Emit a closure function (taking self, args, block; capturing outer_env=UP[0], outer_self=UP[1], outer_block=UP[2])
   and emit code in current buffer to create AilClosure in R3. */
static void emit_closure_fn(Codegen *cg, const ParamList *params, const NodeList *body, bool is_block, Span sp, bool wrap_ail_closure) {
    int fid = next_fn(cg);
    StrBuf fn_buf;
    sb_init(&fn_buf);
    StrBuf *saved = cg->cur;
    int saved_ld = cg->loop_depth;
    cg->cur = &fn_buf;
    cg->loop_depth = 0;

    sb_printf(&fn_buf, "\n.func ail_fn_%d(ref self_in, ref args_in, ref block_in) -> ref\n", fid);
    sb_append(&fn_buf, "    enter 4\n");
    emit_loc(cg, sp);
    sb_printf(&fn_buf, "    mov R4, UP[0]\n");
    sb_printf(&fn_buf, "    call core.env_new (R4, %d) -> (R5)\n", is_block ? 1 : 0);
    sb_append(&fn_buf, "    mov LOC[0], R5\n");
    if (is_block) {
        sb_append(&fn_buf, "    mov R6, UP[1]\n");
        sb_append(&fn_buf, "    mov LOC[1], R6\n");
        sb_append(&fn_buf, "    mov R7, UP[2]\n");
        sb_append(&fn_buf, "    mov LOC[2], R7\n");
    } else {
        sb_append(&fn_buf, "    mov LOC[1], self_in\n");
        sb_append(&fn_buf, "    mov LOC[2], block_in\n");
    }

    /* Bind parameters from args_in */
    if (params && params->len > 0) {
        sb_append(&fn_buf, "    len I0, args_in\n");
        for (size_t i = 0; i < params->len; i++) {
            int l_def = next_lbl(cg);
            int l_set = next_lbl(cg);
            sb_printf(&fn_buf, "    ble I0, %zu, .L%d\n", i, l_def);
            sb_printf(&fn_buf, "    mov R3, args_in[%zu]\n", i);
            sb_printf(&fn_buf, "    jmp .L%d\n", l_set);
            sb_printf(&fn_buf, ".L%d:\n", l_def);
            if (params->items[i].default_val) {
                emit_expr(cg, params->items[i].default_val);
            } else {
                sb_append(&fn_buf, "    mov R3, null\n");
            }
            sb_printf(&fn_buf, ".L%d:\n", l_set);
            sb_append(&fn_buf, "    mov R5, LOC[0]\n");
            sb_append(&fn_buf, "    call core.env_def (R5, ");
            sb_append_escaped(&fn_buf, params->items[i].name, strlen(params->items[i].name));
            sb_append(&fn_buf, ", R3)\n");
        }
    }

    emit_stmts(cg, body);
    sb_append(&fn_buf, "    ret R3\n");
    sb_append(&fn_buf, ".endf\n");

    sb_append_n(&cg->funcs, fn_buf.data, fn_buf.len);
    sb_free(&fn_buf);
    cg->cur = saved;
    cg->loop_depth = saved_ld;

    /* Instantiate closure in current function */
    sb_append(cg->cur, "    mov R4, LOC[0]\n");
    sb_append(cg->cur, "    mov R5, LOC[1]\n");
    sb_append(cg->cur, "    mov R6, LOC[2]\n");
    sb_printf(cg->cur, "    closure ail_fn_%d (R4, R5, R6) -> (R3)\n", fid);
    if (wrap_ail_closure) {
        sb_append(cg->cur, "    call core.mk_closure (R3) -> (R3)\n");
    }
}

/* Emit a kv-list as an MLWSVM map of string -> AilVal in R3 */
static void emit_kv_map(Codegen *cg, const KvList *kl) {
    sb_append(cg->cur, "    mnew R3\n");
    sb_append(cg->cur, "    push R3\n");
    for (size_t i = 0; i < kl->len; i++) {
        emit_expr(cg, kl->items[i].val);
        sb_append(cg->cur, "    mov R4, SP[0]\n");
        sb_append(cg->cur, "    mset R4, ");
        sb_append_escaped(cg->cur, kl->items[i].key, strlen(kl->items[i].key));
        sb_append(cg->cur, ", R3\n");
    }
    sb_append(cg->cur, "    pop R3\n");
}

static void emit_caps(Codegen *cg, const KvList *caps) {
    if (!caps || caps->len == 0) {
        sb_append(cg->cur, "    mov R3, null\n");
        return;
    }
    emit_kv_map(cg, caps);
    sb_append(cg->cur, "    call core.build_caps_map (R3) -> (R3)\n");
}

/* Emit args list (positional + optional kwargs hash as last arg if kwargs present) into R3 */
static void emit_call_args_list(Codegen *cg, const NodeList *args, const KvList *kwargs) {
    sb_append(cg->cur, "    lnew R3\n");
    sb_append(cg->cur, "    push R3\n");
    for (size_t i = 0; i < args->len; i++) {
        emit_expr(cg, args->items[i]);
        sb_append(cg->cur, "    mov R4, SP[0]\n");
        sb_append(cg->cur, "    lpush R4, R3\n");
    }
    if (kwargs && kwargs->len > 0) {
        emit_kv_map(cg, kwargs);
        sb_append(cg->cur, "    mov R4, SP[0]\n");
        sb_append(cg->cur, "    lpush R4, R3\n");
    }
    sb_append(cg->cur, "    pop R3\n");
}

/* Emit tool construction in R3 and bind in current env */
static void emit_tool_decl(Codegen *cg, Node *n, bool bind_in_env) {
    emit_loc(cg, n->span);
    /* Build ParamList from FieldDecl params */
    ParamList pl = {0};
    for (size_t i = 0; i < n->u.tool_nd.param_len; i++) {
        FieldDecl *f = &n->u.tool_nd.params[i];
        paramlist_push(cg->arena, &pl, (Param){
            .name = f->name, .type = f->type, .default_val = f->default_val, .span = f->span
        });
    }
    /* Tool body closure (raw MLWSVM closure for AilTool.fn) */
    emit_closure_fn(cg, &pl, &n->u.tool_nd.body, false, n->span, false);
    sb_append(cg->cur, "    push R3\n"); /* [fn] */

    /* param_names list */
    sb_append(cg->cur, "    lnew R3\n");
    for (size_t i = 0; i < n->u.tool_nd.param_len; i++) {
        sb_append(cg->cur, "    lpush R3, ");
        sb_append_escaped(cg->cur, n->u.tool_nd.params[i].name, strlen(n->u.tool_nd.params[i].name));
        sb_append(cg->cur, "\n");
    }
    sb_append(cg->cur, "    push R3\n"); /* [param_names, fn] */

    /* param_defs map */
    sb_append(cg->cur, "    mnew R3\n");
    sb_append(cg->cur, "    push R3\n"); /* [param_defs, param_names, fn] */
    for (size_t i = 0; i < n->u.tool_nd.param_len; i++) {
        if (n->u.tool_nd.params[i].default_val) {
            emit_expr(cg, n->u.tool_nd.params[i].default_val);
            sb_append(cg->cur, "    mov R4, SP[0]\n");
            sb_append(cg->cur, "    mset R4, ");
            sb_append_escaped(cg->cur, n->u.tool_nd.params[i].name, strlen(n->u.tool_nd.params[i].name));
            sb_append(cg->cur, ", R3\n");
        }
    }

    /* caps grants map */
    emit_caps(cg, &n->u.tool_nd.capabilities);
    sb_append(cg->cur, "    mov R9, R3\n");   /* caps */
    sb_append(cg->cur, "    pop R8\n");       /* param_defs */
    sb_append(cg->cur, "    pop R7\n");       /* param_names */
    sb_append(cg->cur, "    pop R6\n");       /* fn */
    sb_append(cg->cur, "    mov R4, ");
    sb_append_escaped(cg->cur, n->u.tool_nd.name, strlen(n->u.tool_nd.name));
    sb_append(cg->cur, "\n    mov R5, ");
    const char *doc = n->u.tool_nd.doc ? n->u.tool_nd.doc : "";
    sb_append_escaped(cg->cur, doc, strlen(doc));
    sb_append(cg->cur, "\n    mov R10, ");
    const char *sch = n->u.tool_nd.json_schema ? n->u.tool_nd.json_schema : "{}";
    sb_append_escaped(cg->cur, sch, strlen(sch));
    sb_append(cg->cur, "\n    call core.mk_tool (R4, R5, R10, R7, R8, R6, R9) -> (R3)\n");

    if (bind_in_env) {
        sb_append(cg->cur, "    mov R4, LOC[0]\n");
        sb_append(cg->cur, "    call core.env_def (R4, ");
        sb_append_escaped(cg->cur, n->u.tool_nd.name, strlen(n->u.tool_nd.name));
        sb_append(cg->cur, ", R3)\n");
    }
}

/* Check if call is a built-in Module.method like File.read, JSON.parse, Config.get, Env.get, Time.now, Math.sqrt, Random.int */
static bool is_stdlib_module_call(const Node *c, char *out_buf, size_t cap) {
    if (!c->u.call.recv || c->u.call.recv->kind != ND_IDENT) return false;
    const char *m = c->u.call.recv->u.ident.name;
    if (strcmp(m, "File") == 0 || strcmp(m, "Files") == 0 ||
        strcmp(m, "Path") == 0 || strcmp(m, "OS") == 0 ||
        strcmp(m, "JSON") == 0 || strcmp(m, "Config") == 0 ||
        strcmp(m, "Context") == 0 || strcmp(m, "Env") == 0 ||
        strcmp(m, "Time") == 0 || strcmp(m, "Math") == 0 ||
        strcmp(m, "Random") == 0) {
        snprintf(out_buf, cap, "%s.%s", m, c->u.call.name);
        return true;
    }
    return false;
}

static void emit_rescue_try(Codegen *cg, const NodeList *body, const RescueClause *rescues,
                            size_t rescue_len, const NodeList *ensure_body) {
    int l_catch = next_lbl(cg);
    int l_after = next_lbl(cg);
    int l_fin_err = next_lbl(cg);

    bool has_ensure = (ensure_body && ensure_body->len > 0);
    bool has_rescue = (rescue_len > 0);

    if (has_ensure) {
        sb_printf(cg->cur, "    .try .L%d\n", l_fin_err);
    }
    if (has_rescue) {
        sb_printf(cg->cur, "    .try .L%d\n", l_catch);
    }

    emit_stmts(cg, body);

    if (has_rescue) {
        sb_append(cg->cur, "    .endtry\n");
        sb_printf(cg->cur, "    jmp .L%d\n", l_after);
        sb_printf(cg->cur, ".L%d:\n", l_catch);
        sb_append(cg->cur, "    catch R4\n");
        sb_append(cg->cur, "    push R4\n"); /* save caught exception */

        for (size_t i = 0; i < rescue_len; i++) {
            int l_next = next_lbl(cg);
            const RescueClause *rc = &rescues[i];
            sb_append(cg->cur, "    mov R4, SP[0]\n");
            if (rc->exc_type) {
                sb_append(cg->cur, "    call core.exc_matches (R4, ");
                sb_append_escaped(cg->cur, rc->exc_type, strlen(rc->exc_type));
                sb_append(cg->cur, ") -> (I0)\n");
                sb_printf(cg->cur, "    bz I0, .L%d\n", l_next);
            }
            if (rc->var_name) {
                sb_append(cg->cur, "    mov R5, LOC[0]\n");
                sb_append(cg->cur, "    call core.env_set (R5, ");
                sb_append_escaped(cg->cur, rc->var_name, strlen(rc->var_name));
                sb_append(cg->cur, ", R4)\n");
            }
            sb_append(cg->cur, "    pop R4\n");
            emit_stmts(cg, &rc->body);
            sb_printf(cg->cur, "    jmp .L%d\n", l_after);
            sb_printf(cg->cur, ".L%d:\n", l_next);
        }
        /* No rescue clause matched -> rethrow */
        sb_append(cg->cur, "    pop R4\n");
        sb_append(cg->cur, "    throw R4\n");
        sb_printf(cg->cur, ".L%d:\n", l_after);
    }

    if (has_ensure) {
        int l_done = next_lbl(cg);
        sb_append(cg->cur, "    .endtry\n");
        sb_append(cg->cur, "    push R3\n");
        emit_stmts(cg, ensure_body);
        sb_append(cg->cur, "    pop R3\n");
        sb_printf(cg->cur, "    jmp .L%d\n", l_done);
        sb_printf(cg->cur, ".L%d:\n", l_fin_err);
        sb_append(cg->cur, "    catch R4\n");
        sb_append(cg->cur, "    push R4\n");
        emit_stmts(cg, ensure_body);
        sb_append(cg->cur, "    pop R4\n");
        sb_append(cg->cur, "    throw R4\n");
        sb_printf(cg->cur, ".L%d:\n", l_done);
    }
}

static void emit_expr(Codegen *cg, Node *n) {
    if (!n) {
        sb_append(cg->cur, "    mov R3, null\n");
        return;
    }
    emit_loc(cg, n->span);

    switch (n->kind) {
        case ND_NIL:
            sb_append(cg->cur, "    mov R3, null\n");
            break;

        case ND_BOOL:
            sb_printf(cg->cur, "    call core.mk_bool (%d) -> (R3)\n", n->u.bval.val ? 1 : 0);
            break;

        case ND_INT:
            sb_printf(cg->cur, "    call core.mk_int (%lld) -> (R3)\n", (long long)n->u.ival.val);
            break;

        case ND_FLOAT:
            sb_printf(cg->cur, "    call core.mk_float (%.17g) -> (R3)\n", n->u.fval.val);
            break;

        case ND_DECIMAL:
            sb_printf(cg->cur, "    mov R3, %sd\n", n->u.str.text);
            break;

        case ND_STRING:
            sb_append(cg->cur, "    mov R3, ");
            sb_append_escaped(cg->cur, n->u.str.text, n->u.str.len);
            sb_append(cg->cur, "\n");
            break;

        case ND_SYMBOL:
            sb_append(cg->cur, "    call core.mk_sym (");
            sb_append_escaped(cg->cur, n->u.str.text, n->u.str.len);
            sb_append(cg->cur, ") -> (R3)\n");
            break;

        case ND_REGEX:
            sb_append(cg->cur, "    call core.mk_regex (");
            sb_append_escaped(cg->cur, n->u.regex.pattern, strlen(n->u.regex.pattern));
            sb_append(cg->cur, ", ");
            sb_append_escaped(cg->cur, n->u.regex.flags, strlen(n->u.regex.flags));
            sb_append(cg->cur, ") -> (R3)\n");
            break;

        case ND_INTERP:
            sb_append(cg->cur, "    bufnew R3, 0\n");
            sb_append(cg->cur, "    push R3\n");
            for (size_t i = 0; i < n->u.interp.parts.len; i++) {
                emit_expr(cg, n->u.interp.parts.items[i]);
                sb_append(cg->cur, "    call core.to_str (R3) -> (R3)\n");
                sb_append(cg->cur, "    mov R4, SP[0]\n");
                sb_append(cg->cur, "    bufcat R4, R3\n");
            }
            sb_append(cg->cur, "    pop R4\n");
            sb_append(cg->cur, "    strcat R3, R4\n");
            break;

        case ND_ARRAY:
            sb_append(cg->cur, "    lnew R3\n");
            sb_append(cg->cur, "    push R3\n");
            for (size_t i = 0; i < n->u.array.elems.len; i++) {
                emit_expr(cg, n->u.array.elems.items[i]);
                sb_append(cg->cur, "    mov R4, SP[0]\n");
                sb_append(cg->cur, "    lpush R4, R3\n");
            }
            sb_append(cg->cur, "    pop R3\n");
            break;

        case ND_HASH:
            sb_append(cg->cur, "    mnew R3\n");
            sb_append(cg->cur, "    push R3\n");
            for (size_t i = 0; i < n->u.hash.keys.len; i++) {
                emit_expr(cg, n->u.hash.keys.items[i]);
                sb_append(cg->cur, "    call core.to_str (R3) -> (R3)\n");
                sb_append(cg->cur, "    push R3\n");
                emit_expr(cg, n->u.hash.vals.items[i]);
                sb_append(cg->cur, "    pop R5\n");
                sb_append(cg->cur, "    mov R4, SP[0]\n");
                sb_append(cg->cur, "    mset R4, R5, R3\n");
            }
            sb_append(cg->cur, "    pop R3\n");
            break;

        case ND_RANGE:
            emit_expr(cg, n->u.range.start);
            sb_append(cg->cur, "    push R3\n");
            emit_expr(cg, n->u.range.stop);
            sb_append(cg->cur, "    pop R4\n");
            sb_append(cg->cur, "    call core.to_i64 (R4) -> (I0)\n");
            sb_append(cg->cur, "    call core.to_i64 (R3) -> (I1)\n");
            sb_printf(cg->cur, "    call core.mk_range (I0, I1, %d) -> (R3)\n", n->u.range.excl ? 1 : 0);
            break;

        case ND_SELF:
            sb_append(cg->cur, "    mov R3, LOC[1]\n");
            break;

        case ND_IVAR:
            sb_append(cg->cur, "    mov R4, LOC[1]\n");
            sb_append(cg->cur, "    call core.attr_get (R4, ");
            sb_append_escaped(cg->cur, n->u.ident.name, strlen(n->u.ident.name));
            sb_append(cg->cur, ") -> (R3)\n");
            break;

        case ND_IDENT: {
            /* Exception / builtin constant names or env lookup with fallback to zero-arg call */
            int l_found = next_lbl(cg);
            int l_try_self = next_lbl(cg);
            int l_done = next_lbl(cg);
            sb_append(cg->cur, "    mov R4, LOC[0]\n");
            sb_append(cg->cur, "    call core.env_get (R4, ");
            sb_append_escaped(cg->cur, n->u.ident.name, strlen(n->u.ident.name));
            sb_append(cg->cur, ") -> (I0, R3)\n");
            sb_printf(cg->cur, "    bnz I0, .L%d\n", l_found);
            /* If starts with uppercase (e.g. BudgetExceeded, GuardViolation), evaluate to its name string */
            if (n->u.ident.name[0] >= 'A' && n->u.ident.name[0] <= 'Z') {
                sb_append(cg->cur, "    mov R3, ");
                sb_append_escaped(cg->cur, n->u.ident.name, strlen(n->u.ident.name));
                sb_printf(cg->cur, "\n    jmp .L%d\n", l_done);
            } else {
                /* Try zero-arg builtin or method on self */
                sb_append(cg->cur, "    call core.has_builtin (");
                sb_append_escaped(cg->cur, n->u.ident.name, strlen(n->u.ident.name));
                sb_append(cg->cur, ") -> (I1)\n");
                sb_printf(cg->cur, "    bz I1, .L%d\n", l_try_self);
                sb_append(cg->cur, "    lnew R5\n");
                sb_append(cg->cur, "    call core.builtin_call (");
                sb_append_escaped(cg->cur, n->u.ident.name, strlen(n->u.ident.name));
                sb_printf(cg->cur, ", R5, null) -> (R3)\n    jmp .L%d\n", l_done);
                sb_printf(cg->cur, ".L%d:\n", l_try_self);
                sb_append(cg->cur, "    mov R4, LOC[1]\n");
                sb_printf(cg->cur, "    bz.r R4, .L%d\n", l_done);
                sb_append(cg->cur, "    lnew R5\n");
                sb_append(cg->cur, "    call core.send_msg (R4, ");
                sb_append_escaped(cg->cur, n->u.ident.name, strlen(n->u.ident.name));
                sb_printf(cg->cur, ", R5, null) -> (R3)\n    jmp .L%d\n", l_done);
            }
            sb_printf(cg->cur, ".L%d:\n", l_found);
            sb_printf(cg->cur, ".L%d:\n", l_done);
            break;
        }

        case ND_CONST_PATH:
            sb_append(cg->cur, "    mov R3, ");
            sb_append_escaped(cg->cur, n->u.const_path.name, strlen(n->u.const_path.name));
            sb_append(cg->cur, "\n");
            break;

        case ND_ASSIGN: {
            if (n->u.assign.op == TOK_EQ) {
                emit_expr(cg, n->u.assign.val);
            } else if (n->u.assign.op == TOK_OROREQ) {
                int l_keep = next_lbl(cg);
                if (n->u.assign.is_ivar) {
                    sb_append(cg->cur, "    mov R4, LOC[1]\n");
                    sb_append(cg->cur, "    call core.attr_get (R4, ");
                    sb_append_escaped(cg->cur, n->u.assign.name, strlen(n->u.assign.name));
                    sb_append(cg->cur, ") -> (R3)\n");
                } else {
                    sb_append(cg->cur, "    mov R4, LOC[0]\n");
                    sb_append(cg->cur, "    call core.env_get (R4, ");
                    sb_append_escaped(cg->cur, n->u.assign.name, strlen(n->u.assign.name));
                    sb_append(cg->cur, ") -> (I0, R3)\n");
                }
                sb_append(cg->cur, "    call core.is_truthy (R3) -> (I1)\n");
                sb_printf(cg->cur, "    bnz I1, .L%d\n", l_keep);
                emit_expr(cg, n->u.assign.val);
                sb_printf(cg->cur, ".L%d:\n", l_keep);
            } else {
                /* +=, -=, *=, /= */
                if (n->u.assign.is_ivar) {
                    sb_append(cg->cur, "    mov R4, LOC[1]\n");
                    sb_append(cg->cur, "    call core.attr_get (R4, ");
                    sb_append_escaped(cg->cur, n->u.assign.name, strlen(n->u.assign.name));
                    sb_append(cg->cur, ") -> (R3)\n");
                } else {
                    sb_append(cg->cur, "    mov R4, LOC[0]\n");
                    sb_append(cg->cur, "    call core.env_get (R4, ");
                    sb_append_escaped(cg->cur, n->u.assign.name, strlen(n->u.assign.name));
                    sb_append(cg->cur, ") -> (I0, R3)\n");
                }
                sb_append(cg->cur, "    push R3\n");
                emit_expr(cg, n->u.assign.val);
                sb_append(cg->cur, "    pop R4\n");
                const char *fn = "core.op_add";
                if (n->u.assign.op == TOK_MINUSEQ) fn = "core.op_sub";
                else if (n->u.assign.op == TOK_STAREQ) fn = "core.op_mul";
                else if (n->u.assign.op == TOK_SLASHEQ) fn = "core.op_div";
                sb_printf(cg->cur, "    call %s (R4, R3) -> (R3)\n", fn);
            }
            if (n->u.assign.is_ivar) {
                sb_append(cg->cur, "    mov R4, LOC[1]\n");
                sb_append(cg->cur, "    call core.attr_set (R4, ");
                sb_append_escaped(cg->cur, n->u.assign.name, strlen(n->u.assign.name));
                sb_append(cg->cur, ", R3) -> (R3)\n");
            } else {
                sb_append(cg->cur, "    mov R4, LOC[0]\n");
                sb_append(cg->cur, "    call core.env_set (R4, ");
                sb_append_escaped(cg->cur, n->u.assign.name, strlen(n->u.assign.name));
                sb_append(cg->cur, ", R3)\n");
            }
            break;
        }

        case ND_INDEX:
            emit_expr(cg, n->u.idx.obj);
            sb_append(cg->cur, "    push R3\n");
            emit_expr(cg, n->u.idx.idx);
            sb_append(cg->cur, "    pop R4\n");
            sb_append(cg->cur, "    call core.index_get (R4, R3) -> (R3)\n");
            break;

        case ND_INDEX_ASSIGN:
            emit_expr(cg, n->u.index_assign.obj);
            sb_append(cg->cur, "    push R3\n");
            emit_expr(cg, n->u.index_assign.idx);
            sb_append(cg->cur, "    push R3\n");
            emit_expr(cg, n->u.index_assign.val);
            sb_append(cg->cur, "    pop R5\n");
            sb_append(cg->cur, "    pop R4\n");
            sb_append(cg->cur, "    call core.index_set (R4, R5, R3) -> (R3)\n");
            break;

        case ND_ATTR_ASSIGN:
            emit_expr(cg, n->u.attr_assign.obj);
            sb_append(cg->cur, "    push R3\n");
            emit_expr(cg, n->u.attr_assign.val);
            sb_append(cg->cur, "    pop R4\n");
            sb_append(cg->cur, "    call core.attr_set (R4, ");
            sb_append_escaped(cg->cur, n->u.attr_assign.attr, strlen(n->u.attr_assign.attr));
            sb_append(cg->cur, ", R3) -> (R3)\n");
            break;

        case ND_UNARY:
            emit_expr(cg, n->u.unary.expr);
            if (n->u.unary.op == TOK_MINUS) {
                sb_append(cg->cur, "    call core.op_neg (R3) -> (R3)\n");
            } else {
                sb_append(cg->cur, "    call core.op_not (R3) -> (R3)\n");
            }
            break;

        case ND_BINARY: {
            TokKind op = n->u.binary.op;
            if (op == TOK_ANDAND || op == TOK_AND) {
                int l_end = next_lbl(cg);
                emit_expr(cg, n->u.binary.lhs);
                sb_append(cg->cur, "    call core.is_truthy (R3) -> (I0)\n");
                sb_printf(cg->cur, "    bz I0, .L%d\n", l_end);
                emit_expr(cg, n->u.binary.rhs);
                sb_printf(cg->cur, ".L%d:\n", l_end);
                break;
            }
            if (op == TOK_OROR || op == TOK_OR) {
                int l_end = next_lbl(cg);
                emit_expr(cg, n->u.binary.lhs);
                sb_append(cg->cur, "    call core.is_truthy (R3) -> (I0)\n");
                sb_printf(cg->cur, "    bnz I0, .L%d\n", l_end);
                emit_expr(cg, n->u.binary.rhs);
                sb_printf(cg->cur, ".L%d:\n", l_end);
                break;
            }
            emit_expr(cg, n->u.binary.lhs);
            sb_append(cg->cur, "    push R3\n");
            emit_expr(cg, n->u.binary.rhs);
            sb_append(cg->cur, "    pop R4\n");
            const char *fn = "core.op_add";
            switch (op) {
                case TOK_PLUS:       fn = "core.op_add"; break;
                case TOK_MINUS:      fn = "core.op_sub"; break;
                case TOK_STAR:       fn = "core.op_mul"; break;
                case TOK_SLASH:      fn = "core.op_div"; break;
                case TOK_PERCENT:    fn = "core.op_mod"; break;
                case TOK_EQEQ:       fn = "core.op_eq"; break;
                case TOK_BANGEQ:     fn = "core.op_neq"; break;
                case TOK_LT:         fn = "core.op_lt"; break;
                case TOK_LE:         fn = "core.op_le"; break;
                case TOK_GT:         fn = "core.op_gt"; break;
                case TOK_GE:         fn = "core.op_ge"; break;
                case TOK_MATCH:      fn = "core.op_match"; break;
                case TOK_NMATCH:     fn = "core.op_nmatch"; break;
                case TOK_FLOW_ARROW: fn = "stubs.pipe_op"; break;
                case TOK_PIPE:       fn = "stubs.fallback_op"; break;
                case TOK_AMP:        fn = "stubs.parallel_op"; break;
                default: break;
            }
            sb_printf(cg->cur, "    call %s (R4, R3) -> (R3)\n", fn);
            break;
        }

        case ND_LAMBDA:
            emit_closure_fn(cg, &n->u.lambda.params, &n->u.lambda.body, true, n->span, true);
            break;

        case ND_YIELD:
            emit_call_args_list(cg, &n->u.yield_nd.args, NULL);
            sb_append(cg->cur, "    mov R4, LOC[2]\n");
            sb_append(cg->cur, "    call core.call_val (R4, R3, null) -> (R3)\n");
            break;

        case ND_CALL: {
            /* Special built-in `budget(tokens: ..., cost: ..., timeout: ...)` */
            if (!n->u.call.recv && strcmp(n->u.call.name, "budget") == 0) {
                emit_kv_map(cg, &n->u.call.kwargs);
                sb_append(cg->cur, "    call budget.mk_budget (R3) -> (R3)\n");
                break;
            }

            /* Context.new(...) -> ail.context.new_ctx */
            if (n->u.call.recv && n->u.call.recv->kind == ND_IDENT &&
                strcmp(n->u.call.recv->u.ident.name, "Context") == 0 &&
                strcmp(n->u.call.name, "new") == 0) {
                emit_call_args_list(cg, &n->u.call.args, &n->u.call.kwargs);
                sb_append(cg->cur, "    call ctxmod.new_ctx (R3) -> (R3)\n");
                break;
            }

            /* MCP.connect(:binding) -> ail.mcp_client.connect_obj */
            if (n->u.call.recv && n->u.call.recv->kind == ND_IDENT &&
                strcmp(n->u.call.recv->u.ident.name, "MCP") == 0 &&
                strcmp(n->u.call.name, "connect") == 0) {
                emit_call_args_list(cg, &n->u.call.args, &n->u.call.kwargs);
                sb_append(cg->cur, "    call mcpc.connect_obj (R3) -> (R3)\n");
                break;
            }

            /* Knowledge.new(...) -> ail.vector.new_knowledge */
            if (n->u.call.recv && n->u.call.recv->kind == ND_IDENT &&
                strcmp(n->u.call.recv->u.ident.name, "Knowledge") == 0 &&
                strcmp(n->u.call.name, "new") == 0) {
                emit_call_args_list(cg, &n->u.call.args, &n->u.call.kwargs);
                sb_append(cg->cur, "    call vecmod.new_knowledge (R3) -> (R3)\n");
                break;
            }

            /* Stdlib module calls (File.read, Files.glob, JSON.parse, Config.get) */
            char modfn[128];
            if (is_stdlib_module_call(n, modfn, sizeof(modfn))) {
                emit_call_args_list(cg, &n->u.call.args, &n->u.call.kwargs);
                sb_append(cg->cur, "    push R3\n");
                if (n->u.call.has_block) {
                    emit_closure_fn(cg, &n->u.call.block_params, &n->u.call.block_body, true, n->span, true);
                } else {
                    sb_append(cg->cur, "    mov R3, null\n");
                }
                sb_append(cg->cur, "    pop R4\n");
                sb_append(cg->cur, "    call core.builtin_call (");
                sb_append_escaped(cg->cur, modfn, strlen(modfn));
                sb_append(cg->cur, ", R4, R3) -> (R3)\n");
                break;
            }

            /* Method call with receiver */
            if (n->u.call.recv) {
                emit_expr(cg, n->u.call.recv);
                sb_append(cg->cur, "    push R3\n");
                emit_call_args_list(cg, &n->u.call.args, &n->u.call.kwargs);
                sb_append(cg->cur, "    push R3\n");
                if (n->u.call.has_block) {
                    emit_closure_fn(cg, &n->u.call.block_params, &n->u.call.block_body, true, n->span, true);
                } else {
                    sb_append(cg->cur, "    mov R3, null\n");
                }
                sb_append(cg->cur, "    pop R5\n"); /* args */
                sb_append(cg->cur, "    pop R4\n"); /* recv */
                sb_append(cg->cur, "    call core.send_msg (R4, ");
                sb_append_escaped(cg->cur, n->u.call.name, strlen(n->u.call.name));
                sb_append(cg->cur, ", R5, R3) -> (R3)\n");
                break;
            }

            /* Bare call: look up in env (user def/tool/lambda); else self method; else builtin */
            emit_call_args_list(cg, &n->u.call.args, &n->u.call.kwargs);
            sb_append(cg->cur, "    push R3\n");
            if (n->u.call.has_block) {
                emit_closure_fn(cg, &n->u.call.block_params, &n->u.call.block_body, true, n->span, true);
            } else {
                sb_append(cg->cur, "    mov R3, null\n");
            }
            sb_append(cg->cur, "    mov R6, R3\n"); /* block */
            sb_append(cg->cur, "    pop R5\n");     /* args */

            int l_call_env = next_lbl(cg);
            int l_try_bi = next_lbl(cg);
            int l_done = next_lbl(cg);
            sb_append(cg->cur, "    mov R4, LOC[0]\n");
            sb_append(cg->cur, "    call core.env_get (R4, ");
            sb_append_escaped(cg->cur, n->u.call.name, strlen(n->u.call.name));
            sb_append(cg->cur, ") -> (I0, R7)\n");
            sb_printf(cg->cur, "    bnz I0, .L%d\n", l_call_env);

            /* Try method on self if self != null and not a core builtin */
            sb_append(cg->cur, "    mov R8, LOC[1]\n");
            sb_printf(cg->cur, "    bz.r R8, .L%d\n", l_try_bi);
            sb_append(cg->cur, "    call core.has_builtin (");
            sb_append_escaped(cg->cur, n->u.call.name, strlen(n->u.call.name));
            sb_append(cg->cur, ") -> (I1)\n");
            sb_printf(cg->cur, "    bnz I1, .L%d\n", l_try_bi);
            sb_append(cg->cur, "    call core.send_msg (R8, ");
            sb_append_escaped(cg->cur, n->u.call.name, strlen(n->u.call.name));
            sb_printf(cg->cur, ", R5, R6) -> (R3)\n    jmp .L%d\n", l_done);

            sb_printf(cg->cur, ".L%d:\n", l_try_bi);
            sb_append(cg->cur, "    call core.builtin_call (");
            sb_append_escaped(cg->cur, n->u.call.name, strlen(n->u.call.name));
            sb_printf(cg->cur, ", R5, R6) -> (R3)\n    jmp .L%d\n", l_done);

            sb_printf(cg->cur, ".L%d:\n", l_call_env);
            sb_append(cg->cur, "    call core.call_val (R7, R5, R6) -> (R3)\n");
            sb_printf(cg->cur, ".L%d:\n", l_done);
            break;
        }

        case ND_IF: {
            int l_else = next_lbl(cg);
            int l_end = next_lbl(cg);
            emit_expr(cg, n->u.if_nd.cond);
            sb_append(cg->cur, "    call core.is_truthy (R3) -> (I0)\n");
            if (n->u.if_nd.invert) sb_printf(cg->cur, "    bnz I0, .L%d\n", l_else);
            else sb_printf(cg->cur, "    bz I0, .L%d\n", l_else);
            emit_stmts(cg, &n->u.if_nd.then_body);
            sb_printf(cg->cur, "    jmp .L%d\n", l_end);
            sb_printf(cg->cur, ".L%d:\n", l_else);
            if (n->u.if_nd.else_body.len > 0) emit_stmts(cg, &n->u.if_nd.else_body);
            else sb_append(cg->cur, "    mov R3, null\n");
            sb_printf(cg->cur, ".L%d:\n", l_end);
            break;
        }

        case ND_CASE: {
            int l_end = next_lbl(cg);
            if (n->u.case_nd.expr) {
                emit_expr(cg, n->u.case_nd.expr);
                sb_append(cg->cur, "    push R3\n");
            }
            for (size_t i = 0; i < n->u.case_nd.when_len; i++) {
                const WhenClause *wc = &n->u.case_nd.whens[i];
                int l_body = next_lbl(cg);
                int l_next = next_lbl(cg);
                for (size_t j = 0; j < wc->patterns.len; j++) {
                    emit_expr(cg, wc->patterns.items[j]);
                    if (n->u.case_nd.expr) {
                        sb_append(cg->cur, "    mov R4, SP[0]\n");
                        sb_append(cg->cur, "    call core.op_eq (R4, R3) -> (R3)\n");
                    }
                    sb_append(cg->cur, "    call core.is_truthy (R3) -> (I0)\n");
                    sb_printf(cg->cur, "    bnz I0, .L%d\n", l_body);
                }
                sb_printf(cg->cur, "    jmp .L%d\n", l_next);
                sb_printf(cg->cur, ".L%d:\n", l_body);
                if (n->u.case_nd.expr) sb_append(cg->cur, "    drop 1\n");
                emit_stmts(cg, &wc->body);
                sb_printf(cg->cur, "    jmp .L%d\n", l_end);
                sb_printf(cg->cur, ".L%d:\n", l_next);
            }
            if (n->u.case_nd.expr) sb_append(cg->cur, "    drop 1\n");
            if (n->u.case_nd.else_body.len > 0) emit_stmts(cg, &n->u.case_nd.else_body);
            else sb_append(cg->cur, "    mov R3, null\n");
            sb_printf(cg->cur, ".L%d:\n", l_end);
            break;
        }

        case ND_WHILE: {
            int l_top = next_lbl(cg);
            int l_end = next_lbl(cg);
            cg->break_lbl[cg->loop_depth] = l_end;
            cg->next_lbl[cg->loop_depth] = l_top;
            cg->loop_depth++;
            sb_printf(cg->cur, ".L%d:\n", l_top);
            emit_expr(cg, n->u.while_nd.cond);
            sb_append(cg->cur, "    call core.is_truthy (R3) -> (I0)\n");
            if (n->u.while_nd.invert) sb_printf(cg->cur, "    bnz I0, .L%d\n", l_end);
            else sb_printf(cg->cur, "    bz I0, .L%d\n", l_end);
            emit_stmts(cg, &n->u.while_nd.body);
            sb_printf(cg->cur, "    jmp .L%d\n", l_top);
            sb_printf(cg->cur, ".L%d:\n", l_end);
            cg->loop_depth--;
            sb_append(cg->cur, "    mov R3, null\n");
            break;
        }

        case ND_BREAK:
            if (cg->loop_depth > 0) sb_printf(cg->cur, "    jmp .L%d\n", cg->break_lbl[cg->loop_depth - 1]);
            break;

        case ND_NEXT:
            if (cg->loop_depth > 0) sb_printf(cg->cur, "    jmp .L%d\n", cg->next_lbl[cg->loop_depth - 1]);
            break;

        case ND_RETURN:
            if (n->u.ret.val) emit_expr(cg, n->u.ret.val);
            else sb_append(cg->cur, "    mov R3, null\n");
            sb_append(cg->cur, "    ret R3\n");
            break;

        case ND_RAISE:
            if (n->u.raise_nd.exc) {
                emit_expr(cg, n->u.raise_nd.exc);
                sb_append(cg->cur, "    call core.to_str (R3) -> (R3)\n");
                sb_append(cg->cur, "    push R3\n");
                emit_expr(cg, n->u.raise_nd.msg);
                sb_append(cg->cur, "    call core.to_str (R3) -> (R3)\n");
                sb_append(cg->cur, "    pop R4\n");
                sb_append(cg->cur, "    call core.mk_exc (R4, R3) -> (R3)\n");
            } else if (n->u.raise_nd.msg) {
                emit_expr(cg, n->u.raise_nd.msg);
                sb_append(cg->cur, "    call core.to_str (R3) -> (R3)\n");
                sb_append(cg->cur, "    call core.mk_exc (\"RuntimeError\", R3) -> (R3)\n");
            } else {
                sb_append(cg->cur, "    call core.mk_exc (\"RuntimeError\", \"error\") -> (R3)\n");
            }
            sb_append(cg->cur, "    throw R3\n");
            break;

        case ND_BEGIN:
            emit_rescue_try(cg, &n->u.begin_nd.body, n->u.begin_nd.rescues,
                            n->u.begin_nd.rescue_len, &n->u.begin_nd.ensure_body);
            break;

        case ND_DEF:
            emit_closure_fn(cg, &n->u.def_nd.params, &n->u.def_nd.body, false, n->span, true);
            sb_append(cg->cur, "    mov R4, LOC[0]\n");
            sb_append(cg->cur, "    call core.env_def (R4, ");
            sb_append_escaped(cg->cur, n->u.def_nd.name, strlen(n->u.def_nd.name));
            sb_append(cg->cur, ", R3)\n");
            break;

        case ND_CLASS: {
            if (n->u.class_nd.super_name) {
                sb_append(cg->cur, "    mov R4, LOC[0]\n");
                sb_append(cg->cur, "    call core.env_get (R4, ");
                sb_append_escaped(cg->cur, n->u.class_nd.super_name, strlen(n->u.class_nd.super_name));
                sb_append(cg->cur, ") -> (I0, R5)\n");
            } else {
                sb_append(cg->cur, "    mov R5, null\n");
            }
            sb_append(cg->cur, "    call core.mk_class (");
            sb_append_escaped(cg->cur, n->u.class_nd.name, strlen(n->u.class_nd.name));
            sb_append(cg->cur, ", R5) -> (R3)\n");
            sb_append(cg->cur, "    push R3\n");
            sb_append(cg->cur, "    mov R4, LOC[0]\n");
            sb_append(cg->cur, "    call core.env_def (R4, ");
            sb_append_escaped(cg->cur, n->u.class_nd.name, strlen(n->u.class_nd.name));
            sb_append(cg->cur, ", R3)\n");
            for (size_t i = 0; i < n->u.class_nd.body.len; i++) {
                Node *m = n->u.class_nd.body.items[i];
                if (m->kind == ND_DEF) {
                    emit_closure_fn(cg, &m->u.def_nd.params, &m->u.def_nd.body, false, m->span, false);
                    sb_append(cg->cur, "    mov R4, SP[0]\n");
                    sb_append(cg->cur, "    call core.class_def_method (R4, ");
                    sb_append_escaped(cg->cur, m->u.def_nd.name, strlen(m->u.def_nd.name));
                    sb_append(cg->cur, ", R3)\n");
                }
            }
            sb_append(cg->cur, "    pop R3\n");
            break;
        }

        case ND_MODULE:
        case ND_INCLUDE:
            sb_append(cg->cur, "    mov R3, null\n");
            break;

        /* ---- First-Class AIL Constructs ---- */

        case ND_REQUIRE:
        case ND_USE:
            /* Handled during compile-time loading / preflight */
            sb_append(cg->cur, "    mov R3, null\n");
            break;

        case ND_SCHEMA: {
            sb_append(cg->cur, "    lnew R3\n");
            sb_append(cg->cur, "    push R3\n");
            for (size_t i = 0; i < n->u.schema_nd.field_len; i++) {
                const FieldDecl *f = &n->u.schema_nd.fields[i];
                if (f->default_val) emit_expr(cg, f->default_val);
                else sb_append(cg->cur, "    mov R3, null\n");
                sb_append(cg->cur, "    lnew R5\n");
                sb_append(cg->cur, "    lpush R5, ");
                sb_append_escaped(cg->cur, f->name, strlen(f->name));
                sb_append(cg->cur, ", \"any\", R3, ");
                int req = (!f->default_val && (!f->type || f->type->kind != TY_OPTIONAL)) ? 1 : 0;
                sb_printf(cg->cur, "%d\n", req);
                sb_append(cg->cur, "    mov R4, SP[0]\n");
                sb_append(cg->cur, "    lpush R4, R5\n");
            }
            sb_append(cg->cur, "    pop R5\n");
            sb_append(cg->cur, "    mov R4, ");
            sb_append_escaped(cg->cur, n->u.schema_nd.name, strlen(n->u.schema_nd.name));
            sb_append(cg->cur, "\n    mov R6, ");
            const char *js = n->u.schema_nd.json_schema ? n->u.schema_nd.json_schema : "{}";
            sb_append_escaped(cg->cur, js, strlen(js));
            sb_append(cg->cur, "\n    call core.mk_schema (R4, R5, R6) -> (R3)\n");
            sb_append(cg->cur, "    mov R4, LOC[0]\n");
            sb_append(cg->cur, "    call core.env_def (R4, ");
            sb_append_escaped(cg->cur, n->u.schema_nd.name, strlen(n->u.schema_nd.name));
            sb_append(cg->cur, ", R3)\n");
            break;
        }

        case ND_TOOL:
            emit_tool_decl(cg, n, true);
            break;

        case ND_GUARD: {
            /* Emit on_input, on_output, on_tool closures + caps */
            sb_append(cg->cur, "    mov R3, null\n    push R3\n    push R3\n    push R3\n");
            /* SP[2]=on_input, SP[1]=on_output, SP[0]=on_tool */
            for (size_t i = 0; i < n->u.guard_nd.hook_len; i++) {
                GuardHook *h = &n->u.guard_nd.hooks[i];
                emit_closure_fn(cg, &h->params, &h->body, false, h->span, false);
                int slot = (h->kind == GUARD_INPUT) ? 2 : (h->kind == GUARD_OUTPUT ? 1 : 0);
                sb_printf(cg->cur, "    mov SP[%d], R3\n", slot);
            }
            emit_caps(cg, &n->u.guard_nd.capabilities);
            sb_append(cg->cur, "    mov R8, R3\n");
            sb_append(cg->cur, "    pop R7\n"); /* on_tool */
            sb_append(cg->cur, "    pop R6\n"); /* on_output */
            sb_append(cg->cur, "    pop R5\n"); /* on_input */
            sb_append(cg->cur, "    mov R4, ");
            sb_append_escaped(cg->cur, n->u.guard_nd.name, strlen(n->u.guard_nd.name));
            sb_append(cg->cur, "\n    call core.mk_guard (R4, R5, R6, R7, R8) -> (R3)\n");
            sb_append(cg->cur, "    mov R4, LOC[0]\n");
            sb_append(cg->cur, "    call core.env_def (R4, ");
            sb_append_escaped(cg->cur, n->u.guard_nd.name, strlen(n->u.guard_nd.name));
            sb_append(cg->cur, ", R3)\n");
            break;
        }

        case ND_WITHIN: {
            /* Wrap the within body execution in optional rescue/ensure */
            Node *runner = node_new(cg->arena, ND_BLOCK, n->span);
            (void)runner;
            int l_catch = next_lbl(cg);
            int l_after = next_lbl(cg);
            int l_fin_err = next_lbl(cg);
            bool has_rescue = n->u.within_nd.rescue_len > 0;
            bool has_ensure = n->u.within_nd.ensure_body.len > 0;
            if (has_ensure) sb_printf(cg->cur, "    .try .L%d\n", l_fin_err);
            if (has_rescue) sb_printf(cg->cur, "    .try .L%d\n", l_catch);

            emit_expr(cg, n->u.within_nd.spec);
            sb_append(cg->cur, "    push R3\n");
            emit_closure_fn(cg, NULL, &n->u.within_nd.body, true, n->span, false);
            sb_append(cg->cur, "    pop R4\n");
            sb_append(cg->cur, "    call budget.run_within (R4, R3) -> (R3)\n");

            if (has_rescue) {
                sb_append(cg->cur, "    .endtry\n");
                sb_printf(cg->cur, "    jmp .L%d\n", l_after);
                sb_printf(cg->cur, ".L%d:\n", l_catch);
                sb_append(cg->cur, "    catch R4\n    push R4\n");
                for (size_t i = 0; i < n->u.within_nd.rescue_len; i++) {
                    int l_next = next_lbl(cg);
                    const RescueClause *rc = &n->u.within_nd.rescues[i];
                    sb_append(cg->cur, "    mov R4, SP[0]\n");
                    if (rc->exc_type) {
                        sb_append(cg->cur, "    call core.exc_matches (R4, ");
                        sb_append_escaped(cg->cur, rc->exc_type, strlen(rc->exc_type));
                        sb_append(cg->cur, ") -> (I0)\n");
                        sb_printf(cg->cur, "    bz I0, .L%d\n", l_next);
                    }
                    if (rc->var_name) {
                        sb_append(cg->cur, "    mov R5, LOC[0]\n");
                        sb_append(cg->cur, "    call core.env_set (R5, ");
                        sb_append_escaped(cg->cur, rc->var_name, strlen(rc->var_name));
                        sb_append(cg->cur, ", R4)\n");
                    }
                    sb_append(cg->cur, "    pop R4\n");
                    emit_stmts(cg, &rc->body);
                    sb_printf(cg->cur, "    jmp .L%d\n", l_after);
                    sb_printf(cg->cur, ".L%d:\n", l_next);
                }
                sb_append(cg->cur, "    pop R4\n    throw R4\n");
                sb_printf(cg->cur, ".L%d:\n", l_after);
            }
            if (has_ensure) {
                int l_done = next_lbl(cg);
                sb_append(cg->cur, "    .endtry\n    push R3\n");
                emit_stmts(cg, &n->u.within_nd.ensure_body);
                sb_append(cg->cur, "    pop R3\n");
                sb_printf(cg->cur, "    jmp .L%d\n", l_done);
                sb_printf(cg->cur, ".L%d:\n", l_fin_err);
                sb_append(cg->cur, "    catch R4\n    push R4\n");
                emit_stmts(cg, &n->u.within_nd.ensure_body);
                sb_append(cg->cur, "    pop R4\n    throw R4\n");
                sb_printf(cg->cur, ".L%d:\n", l_done);
            }
            break;
        }

        case ND_RETRY: {
            emit_expr(cg, n->u.retry_nd.count);
            sb_append(cg->cur, "    push R3\n");
            emit_kv_map(cg, &n->u.retry_nd.opts);
            sb_append(cg->cur, "    push R3\n");
            ParamList pl = {0};
            if (n->u.retry_nd.attempt_var) {
                paramlist_push(cg->arena, &pl, (Param){ .name = n->u.retry_nd.attempt_var, .span = n->span });
            }
            emit_closure_fn(cg, &pl, &n->u.retry_nd.body, true, n->span, false);
            sb_append(cg->cur, "    pop R5\n"); /* opts */
            sb_append(cg->cur, "    pop R4\n"); /* count */
            sb_append(cg->cur, "    call budget.run_retry (R4, R5, R3) -> (R3)\n");
            break;
        }

        case ND_MCP_SERVER: {
            sb_append(cg->cur, "    mov R4, ");
            sb_append_escaped(cg->cur, n->u.mcp_nd.name, strlen(n->u.mcp_nd.name));
            sb_append(cg->cur, "\n    mov R5, ");
            sb_append_escaped(cg->cur, n->u.mcp_nd.version, strlen(n->u.mcp_nd.version));
            sb_append(cg->cur, "\n    call mcp.new_server (R4, R5) -> (R3)\n");
            sb_append(cg->cur, "    push R3\n"); /* SP[0] = srv */

            if (n->u.mcp_nd.capabilities.len > 0) {
                emit_caps(cg, &n->u.mcp_nd.capabilities);
                sb_append(cg->cur, "    mov R4, SP[0]\n");
                sb_append(cg->cur, "    call mcp.set_caps (R4, R3)\n");
            }
            for (size_t i = 0; i < n->u.mcp_nd.exposed_len; i++) {
                sb_append(cg->cur, "    mov R4, LOC[0]\n");
                sb_append(cg->cur, "    call core.env_get (R4, ");
                sb_append_escaped(cg->cur, n->u.mcp_nd.exposed[i], strlen(n->u.mcp_nd.exposed[i]));
                sb_append(cg->cur, ") -> (I0, R3)\n");
                sb_append(cg->cur, "    mov R4, SP[0]\n");
                sb_append(cg->cur, "    call mcp.add_tool (R4, R3)\n");
            }
            for (size_t i = 0; i < n->u.mcp_nd.inline_tools.len; i++) {
                emit_tool_decl(cg, n->u.mcp_nd.inline_tools.items[i], false);
                sb_append(cg->cur, "    mov R4, SP[0]\n");
                sb_append(cg->cur, "    call mcp.add_tool (R4, R3)\n");
            }
            for (size_t i = 0; i < n->u.mcp_nd.resource_len; i++) {
                const McpResourceDecl *r = &n->u.mcp_nd.resources[i];
                emit_closure_fn(cg, NULL, &r->body, false, r->span, false);
                sb_append(cg->cur, "    mov R4, SP[0]\n");
                sb_append(cg->cur, "    call mcp.add_resource (R4, ");
                sb_append_escaped(cg->cur, r->uri, strlen(r->uri));
                sb_append(cg->cur, ", ");
                sb_append_escaped(cg->cur, r->mime, strlen(r->mime));
                sb_append(cg->cur, ", R3)\n");
            }
            for (size_t i = 0; i < n->u.mcp_nd.prompt_len; i++) {
                const McpPromptDecl *pr = &n->u.mcp_nd.prompts[i];
                emit_closure_fn(cg, NULL, &pr->body, false, pr->span, false);
                sb_append(cg->cur, "    mov R4, SP[0]\n");
                sb_append(cg->cur, "    call mcp.add_prompt (R4, ");
                sb_append_escaped(cg->cur, pr->name, strlen(pr->name));
                sb_append(cg->cur, ", ");
                const char *doc = pr->doc ? pr->doc : "";
                sb_append_escaped(cg->cur, doc, strlen(doc));
                sb_append(cg->cur, ", R3)\n");
            }

            sb_append(cg->cur, "    pop R3\n");
            sb_append(cg->cur, "    call mcp.serve (R3)\n");
            sb_append(cg->cur, "    mov R3, null\n");
            break;
        }

        /* ---- v0.2 live Skill & Agent declarations ---- */

        case ND_SKILL:
            if (n->u.skill_nd.instructions) emit_expr(cg, n->u.skill_nd.instructions);
            else sb_append(cg->cur, "    mov R3, \"\"\n");
            sb_append(cg->cur, "    push R3\n");
            if (n->u.skill_nd.tools) emit_expr(cg, n->u.skill_nd.tools);
            else sb_append(cg->cur, "    mov R3, null\n");
            sb_append(cg->cur, "    push R3\n");
            emit_caps(cg, &n->u.skill_nd.capabilities);
            sb_append(cg->cur, "    mov R8, R3\n");
            sb_append(cg->cur, "    pop R7\n"); /* tools */
            sb_append(cg->cur, "    pop R6\n"); /* instructions */
            sb_append(cg->cur, "    mov R4, ");
            sb_append_escaped(cg->cur, n->u.skill_nd.name, strlen(n->u.skill_nd.name));
            sb_append(cg->cur, "\n    mov R5, ");
            sb_append_escaped(cg->cur, n->u.skill_nd.doc ? n->u.skill_nd.doc : "",
                              n->u.skill_nd.doc ? strlen(n->u.skill_nd.doc) : 0);
            sb_append(cg->cur, "\n    call skill.mk_inline_skill (R4, R5, R6, R7, R8) -> (R3)\n");
            sb_append(cg->cur, "    mov R4, LOC[0]\n");
            sb_append(cg->cur, "    call core.env_def (R4, ");
            sb_append_escaped(cg->cur, n->u.skill_nd.name, strlen(n->u.skill_nd.name));
            sb_append(cg->cur, ", R3)\n");
            break;

        case ND_AGENT:
            sb_append(cg->cur, "    mnew R3\n");
            sb_append(cg->cur, "    push R3\n"); /* SP[0] = agent fields map */
            if (n->u.agent_nd.model) {
                emit_expr(cg, n->u.agent_nd.model);
                sb_append(cg->cur, "    mov R4, SP[0]\n    mset R4, \"model\", R3\n");
            }
            if (n->u.agent_nd.context_view) {
                emit_expr(cg, n->u.agent_nd.context_view);
                sb_append(cg->cur, "    mov R4, SP[0]\n    mset R4, \"context_view\", R3\n");
            }
            if (n->u.agent_nd.tools) {
                emit_expr(cg, n->u.agent_nd.tools);
                sb_append(cg->cur, "    mov R4, SP[0]\n    mset R4, \"tools\", R3\n");
            }
            if (n->u.agent_nd.mcp) {
                emit_expr(cg, n->u.agent_nd.mcp);
                sb_append(cg->cur, "    mov R4, SP[0]\n    mset R4, \"mcp\", R3\n");
            }
            if (n->u.agent_nd.skills) {
                emit_expr(cg, n->u.agent_nd.skills);
                sb_append(cg->cur, "    mov R4, SP[0]\n    mset R4, \"skills\", R3\n");
            }
            if (n->u.agent_nd.guards) {
                emit_expr(cg, n->u.agent_nd.guards);
                sb_append(cg->cur, "    mov R4, SP[0]\n    mset R4, \"guards\", R3\n");
            }
            if (n->u.agent_nd.system_prompt) {
                emit_expr(cg, n->u.agent_nd.system_prompt);
                sb_append(cg->cur, "    mov R4, SP[0]\n    mset R4, \"system\", R3\n");
            }
            if (n->u.agent_nd.ret_type && n->u.agent_nd.ret_type->kind == TY_NAMED) {
                sb_append(cg->cur, "    mov R4, LOC[0]\n    call core.env_get (R4, ");
                sb_append_escaped(cg->cur, n->u.agent_nd.ret_type->name, strlen(n->u.agent_nd.ret_type->name));
                sb_append(cg->cur, ") -> (I0, R3)\n    mov R4, SP[0]\n    mset R4, \"returns\", R3\n");
            }
            if (n->u.agent_nd.capabilities.len > 0) {
                emit_caps(cg, &n->u.agent_nd.capabilities);
                sb_append(cg->cur, "    mov R4, SP[0]\n    mset R4, \"caps\", R3\n");
            }
            if (n->u.agent_nd.limits.len > 0) {
                emit_kv_map(cg, &n->u.agent_nd.limits);
                sb_append(cg->cur, "    mov R4, SP[0]\n    mset R4, \"limits\", R3\n");
            }
            sb_append(cg->cur, "    pop R5\n");
            sb_append(cg->cur, "    mov R4, ");
            sb_append_escaped(cg->cur, n->u.agent_nd.name, strlen(n->u.agent_nd.name));
            sb_append(cg->cur, "\n    call agmod.mk_agent (R4, R5) -> (R3)\n");
            sb_append(cg->cur, "    mov R4, LOC[0]\n");
            sb_append(cg->cur, "    call core.env_def (R4, ");
            sb_append_escaped(cg->cur, n->u.agent_nd.name, strlen(n->u.agent_nd.name));
            sb_append(cg->cur, ", R3)\n");
            break;

        case ND_FLOW: {
            sb_append(cg->cur, "    mnew R3\n");
            sb_append(cg->cur, "    push R3\n"); /* SP[0] = fields map */
            if (n->u.flow_nd.start_node) {
                sb_append(cg->cur, "    mov R4, SP[0]\n    mset R4, \"start\", ");
                sb_append_escaped(cg->cur, n->u.flow_nd.start_node, strlen(n->u.flow_nd.start_node));
                sb_append(cg->cur, "\n");
            }
            sb_append(cg->cur, "    mov R4, SP[0]\n    mov R5, LOC[0]\n    mset R4, \"env\", R5\n");

            /* nodes map */
            sb_append(cg->cur, "    mnew R3\n    push R3\n"); /* SP[0] = nodes, SP[1] = fields */
            for (size_t i = 0; i < n->u.flow_nd.node_len; i++) {
                FlowNodeDecl *fn = &n->u.flow_nd.nodes[i];
                if (fn->handler && fn->handler->kind == ND_DECIDE) {
                    Node *dec = fn->handler;
                    sb_append(cg->cur, "    mnew R3\n    push R3\n");
                    sb_append(cg->cur, "    mov R4, SP[0]\n    mset R4, \"__decide__\", 1\n");
                    if (dec->u.decide_nd.using_model) {
                        emit_expr(cg, dec->u.decide_nd.using_model);
                        sb_append(cg->cur, "    mov R4, SP[0]\n    mset R4, \"model\", R3\n");
                    }
                    const char *js = dec->u.decide_nd.json_schema ? dec->u.decide_nd.json_schema : "{}";
                    sb_append(cg->cur, "    mov R4, SP[0]\n    mset R4, \"schema\", ");
                    sb_append_escaped(cg->cur, js, strlen(js));
                    sb_append(cg->cur, "\n    pop R3\n");
                } else if (fn->handler) {
                    emit_expr(cg, fn->handler);
                } else {
                    sb_append(cg->cur, "    mov R3, null\n");
                }
                sb_append(cg->cur, "    mov R4, SP[0]\n    mset R4, ");
                sb_append_escaped(cg->cur, fn->name, strlen(fn->name));
                sb_append(cg->cur, ", R3\n");
            }
            sb_append(cg->cur, "    pop R3\n    mov R4, SP[0]\n    mset R4, \"nodes\", R3\n");

            /* edges list */
            sb_append(cg->cur, "    lnew R3\n    push R3\n"); /* SP[0] = edges, SP[1] = fields */
            for (size_t i = 0; i < n->u.flow_nd.edge_len; i++) {
                FlowEdgeDecl *e = &n->u.flow_nd.edges[i];
                sb_append(cg->cur, "    mnew R3\n    push R3\n"); /* SP[0] = edge map */
                sb_append(cg->cur, "    mov R4, SP[0]\n    mset R4, \"from\", ");
                sb_append_escaped(cg->cur, e->from, strlen(e->from));
                sb_append(cg->cur, "\n    mset R4, \"to\", ");
                sb_append_escaped(cg->cur, e->to, strlen(e->to));
                sb_printf(cg->cur, "\n    mset R4, \"invert\", %d\n", e->invert_cond ? 1 : 0);
                if (e->cond) {
                    emit_expr(cg, e->cond);
                } else {
                    sb_append(cg->cur, "    mov R3, null\n");
                }
                sb_append(cg->cur, "    mov R4, SP[0]\n    mset R4, \"cond\", R3\n");
                if (e->max_bound) {
                    emit_expr(cg, e->max_bound);
                } else {
                    sb_append(cg->cur, "    mov R3, null\n");
                }
                sb_append(cg->cur, "    mov R4, SP[0]\n    mset R4, \"max\", R3\n");
                sb_append(cg->cur, "    pop R3\n    mov R4, SP[0]\n    lpush R4, R3\n");
            }
            sb_append(cg->cur, "    pop R3\n    mov R4, SP[0]\n    mset R4, \"edges\", R3\n");

            sb_append(cg->cur, "    pop R5\n");
            sb_append(cg->cur, "    mov R4, ");
            sb_append_escaped(cg->cur, n->u.flow_nd.name, strlen(n->u.flow_nd.name));
            sb_append(cg->cur, "\n    call flowmod.mk_flow (R4, R5) -> (R3)\n");
            sb_append(cg->cur, "    mov R4, LOC[0]\n");
            sb_append(cg->cur, "    call core.env_def (R4, ");
            sb_append_escaped(cg->cur, n->u.flow_nd.name, strlen(n->u.flow_nd.name));
            sb_append(cg->cur, ", R3)\n");
            break;
        }

        case ND_DECIDE:
            emit_expr(cg, n->u.decide_nd.using_model);
            sb_append(cg->cur, "    push R3\n");
            emit_expr(cg, n->u.decide_nd.on_expr);
            sb_append(cg->cur, "    pop R4\n");
            sb_append(cg->cur, "    mov R5, ");
            sb_append_escaped(cg->cur, n->u.decide_nd.json_schema ? n->u.decide_nd.json_schema : "{}",
                              n->u.decide_nd.json_schema ? strlen(n->u.decide_nd.json_schema) : 2);
            sb_append(cg->cur, "\n    call s1.run_decide (R4, R3, R5) -> (R3)\n");
            break;

        case ND_A2A_AGENT:
            if (n->u.a2a_nd.description) emit_expr(cg, n->u.a2a_nd.description);
            else sb_append(cg->cur, "    mov R3, \"\"\n");
            sb_append(cg->cur, "    push R3\n");
            if (n->u.a2a_nd.handler) emit_expr(cg, n->u.a2a_nd.handler);
            else sb_append(cg->cur, "    mov R3, null\n");
            sb_append(cg->cur, "    pop R4\n");
            sb_append(cg->cur, "    call a2amod.serve (");
            sb_append_escaped(cg->cur, n->u.a2a_nd.name, strlen(n->u.a2a_nd.name));
            sb_append(cg->cur, ", ");
            sb_append_escaped(cg->cur, n->u.a2a_nd.version, strlen(n->u.a2a_nd.version));
            sb_append(cg->cur, ", R4, R3)\n");
            sb_append(cg->cur, "    mov R3, null\n");
            break;

        case ND_BLOCK:
        case ND_PROGRAM:
            emit_stmts(cg, &n->u.block.stmts);
            break;
    }
}

static void emit_stmts(Codegen *cg, const NodeList *nl) {
    if (!nl || nl->len == 0) {
        sb_append(cg->cur, "    mov R3, null\n");
        return;
    }
    for (size_t i = 0; i < nl->len; i++) {
        emit_expr(cg, nl->items[i]);
    }
}

bool codegen_module(const char *source_file, Node *program, const SemaInfo *sema,
                    Arena *arena, DiagList *diags, StrBuf *out_masm) {
    Codegen cg = {
        .source_file = source_file,
        .sema = sema,
        .arena = arena,
        .diags = diags,
        .lbl_seq = 0,
        .fn_seq = 0
    };
    sb_init(&cg.funcs);

    StrBuf main_buf;
    sb_init(&main_buf);
    cg.cur = &main_buf;

    sb_append(&main_buf, "\n.func main -> i64\n");
    sb_append(&main_buf, "    enter 4\n");
    sb_append(&main_buf, "    call core.init ()\n");
    sb_append(&main_buf, "    call core.env_new (null, 0) -> (R4)\n");
    sb_append(&main_buf, "    mov LOC[0], R4\n");
    sb_append(&main_buf, "    mov.r LOC[1], null\n");
    sb_append(&main_buf, "    mov.r LOC[2], null\n");
    sb_append(&main_buf, "    .try .Lmain_uncaught\n");

    emit_stmts(&cg, &program->u.block.stmts);

    sb_append(&main_buf, "    .endtry\n");
    sb_append(&main_buf, "    ret 0\n");
    sb_append(&main_buf, ".Lmain_uncaught:\n");
    sb_append(&main_buf, "    catch R4\n");
    sb_append(&main_buf, "    bnisa R4, core.AilExc, .Lmain_rethrow\n");
    sb_append(&main_buf, "    new R5, Error\n");
    sb_append(&main_buf, "    mov R5->Error.code, R4->core.AilExc.klass\n");
    sb_append(&main_buf, "    mov R5->Error.message, R4->core.AilExc.message\n");
    sb_append(&main_buf, "    throw R5\n");
    sb_append(&main_buf, ".Lmain_rethrow:\n");
    sb_append(&main_buf, "    throw R4\n");
    sb_append(&main_buf, ".endf\n");

    /* Assemble final .masm output */
    sb_printf(out_masm, "; Generated by ail 0.5.0 from %s\n", source_file ? source_file : "<input>");
    sb_append(out_masm, ".import ail.core as core\n");
    sb_append(out_masm, ".import ail.config as cfg\n");
    sb_append(out_masm, ".import ail.budget as budget\n");
    sb_append(out_masm, ".import ail.sandbox as sandbox\n");
    sb_append(out_masm, ".import ail.mcp as mcp\n");
    sb_append(out_masm, ".import ail.mcp_client as mcpc\n");
    sb_append(out_masm, ".import ail.skill as skill\n");
    sb_append(out_masm, ".import ail.llm as llm\n");
    sb_append(out_masm, ".import ail.s1 as s1\n");
    sb_append(out_masm, ".import ail.vector as vecmod\n");
    sb_append(out_masm, ".import ail.context as ctxmod\n");
    sb_append(out_masm, ".import ail.agent as agmod\n");
    sb_append(out_masm, ".import ail.flow as flowmod\n");
    sb_append(out_masm, ".import ail.a2a as a2amod\n");
    sb_append(out_masm, ".import ail.stubs as stubs\n");
    sb_append(out_masm, ".debug_format core.debug_format\n");

    if (cg.funcs.len > 0) sb_append_n(out_masm, cg.funcs.data, cg.funcs.len);
    sb_append_n(out_masm, main_buf.data, main_buf.len);

    sb_free(&cg.funcs);
    sb_free(&main_buf);
    return diags->error_count == 0;
}
