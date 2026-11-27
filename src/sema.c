#include "sema.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    Arena *arena;
    DiagList *diags;
    SemaInfo *info;
} Sema;

static Node *find_schema(const Sema *s, const char *name) {
    for (size_t i = 0; i < s->info->schema_len; i++) {
        if (strcmp(s->info->schemas[i]->u.schema_nd.name, name) == 0)
            return s->info->schemas[i];
    }
    return NULL;
}

static Node *find_tool(const Sema *s, const char *name) {
    for (size_t i = 0; i < s->info->tool_len; i++) {
        if (strcmp(s->info->tools[i]->u.tool_nd.name, name) == 0)
            return s->info->tools[i];
    }
    return NULL;
}

static bool is_valid_binding_kind(const char *k) {
    static const char *valid[] = {
        "llm", "s1", "embedder", "db", "vector_db", "mcp", "skills"
    };
    for (size_t i = 0; i < sizeof(valid)/sizeof(valid[0]); i++) {
        if (strcmp(k, valid[i]) == 0) return true;
    }
    return false;
}

/* ---- Compile-time JSON Schema generation ---- */

static void emit_type_json_schema(Sema *s, const TypeExpr *ty, StrBuf *sb, int depth) {
    if (!ty || depth > 8) {
        sb_append(sb, "{}");
        return;
    }
    if (ty->kind == TY_OPTIONAL) {
        emit_type_json_schema(s, ty->args[0], sb, depth + 1);
        return;
    }
    if (ty->kind == TY_ENUM) {
        sb_append(sb, "{\"type\":\"string\",\"enum\":[");
        for (size_t i = 0; i < ty->enum_len; i++) {
            if (i > 0) sb_append_ch(sb, ',');
            sb_append_escaped(sb, ty->enum_vals[i], strlen(ty->enum_vals[i]));
        }
        sb_append(sb, "]}");
        return;
    }
    if (ty->kind == TY_GENERIC) {
        if (strcmp(ty->name, "Array") == 0 && ty->arg_len == 1) {
            sb_append(sb, "{\"type\":\"array\",\"items\":");
            emit_type_json_schema(s, ty->args[0], sb, depth + 1);
            sb_append_ch(sb, '}');
            return;
        }
        if (strcmp(ty->name, "Hash") == 0 && ty->arg_len == 2) {
            sb_append(sb, "{\"type\":\"object\",\"additionalProperties\":");
            emit_type_json_schema(s, ty->args[1], sb, depth + 1);
            sb_append_ch(sb, '}');
            return;
        }
        if (strcmp(ty->name, "Optional") == 0 && ty->arg_len == 1) {
            emit_type_json_schema(s, ty->args[0], sb, depth + 1);
            return;
        }
        diag_error(s->diags, ty->span, "unknown generic boundary type '%s'", ty->name);
        sb_append(sb, "{}");
        return;
    }

    /* TY_NAMED */
    if (strcmp(ty->name, "String") == 0) { sb_append(sb, "{\"type\":\"string\"}"); return; }
    if (strcmp(ty->name, "Int") == 0) { sb_append(sb, "{\"type\":\"integer\"}"); return; }
    if (strcmp(ty->name, "Float") == 0 || strcmp(ty->name, "Decimal") == 0) {
        sb_append(sb, "{\"type\":\"number\"}");
        return;
    }
    if (strcmp(ty->name, "Bool") == 0) { sb_append(sb, "{\"type\":\"boolean\"}"); return; }
    if (strcmp(ty->name, "Any") == 0) { sb_append(sb, "{}"); return; }

    Node *sc = find_schema(s, ty->name);
    if (sc) {
        sb_append(sb, "{\"type\":\"object\",\"properties\":{");
        for (size_t i = 0; i < sc->u.schema_nd.field_len; i++) {
            const FieldDecl *f = &sc->u.schema_nd.fields[i];
            if (i > 0) sb_append_ch(sb, ',');
            sb_append_escaped(sb, f->name, strlen(f->name));
            sb_append_ch(sb, ':');
            emit_type_json_schema(s, f->type, sb, depth + 1);
        }
        sb_append(sb, "},\"required\":[");
        bool first = true;
        for (size_t i = 0; i < sc->u.schema_nd.field_len; i++) {
            const FieldDecl *f = &sc->u.schema_nd.fields[i];
            if (!f->default_val && (!f->type || f->type->kind != TY_OPTIONAL)) {
                if (!first) sb_append_ch(sb, ',');
                first = false;
                sb_append_escaped(sb, f->name, strlen(f->name));
            }
        }
        sb_append(sb, "]}");
        return;
    }

    diag_error(s->diags, ty->span, "unknown boundary type '%s'", ty->name);
    sb_append(sb, "{}");
}

static char *build_fields_json_schema(Sema *s, const FieldDecl *fields, size_t n) {
    StrBuf sb;
    sb_init(&sb);
    sb_append(&sb, "{\"type\":\"object\",\"properties\":{");
    for (size_t i = 0; i < n; i++) {
        const FieldDecl *f = &fields[i];
        if (i > 0) sb_append_ch(&sb, ',');
        sb_append_escaped(&sb, f->name, strlen(f->name));
        sb_append_ch(&sb, ':');
        if (f->doc) {
            /* Merge description into schema object */
            StrBuf inner;
            sb_init(&inner);
            emit_type_json_schema(s, f->type, &inner, 0);
            if (inner.len >= 2 && inner.data[inner.len - 1] == '}') {
                sb_append_n(&sb, inner.data, inner.len - 1);
                if (inner.len > 2) sb_append_ch(&sb, ',');
                sb_append(&sb, "\"description\":");
                sb_append_escaped(&sb, f->doc, strlen(f->doc));
                sb_append_ch(&sb, '}');
            } else {
                sb_append(&sb, inner.data ? inner.data : "{}");
            }
            sb_free(&inner);
        } else {
            emit_type_json_schema(s, f->type, &sb, 0);
        }
    }
    sb_append(&sb, "},\"required\":[");
    bool first = true;
    for (size_t i = 0; i < n; i++) {
        const FieldDecl *f = &fields[i];
        if (!f->default_val && (!f->type || f->type->kind != TY_OPTIONAL)) {
            if (!first) sb_append_ch(&sb, ',');
            first = false;
            sb_append_escaped(&sb, f->name, strlen(f->name));
        }
    }
    sb_append(&sb, "]}");
    char *out = arena_strndup(s->arena, sb.data ? sb.data : "{}", sb.len);
    sb_free(&sb);
    return out;
}

static char *build_decide_json_schema(Sema *s, const Node *dec) {
    StrBuf sb;
    sb_init(&sb);
    sb_append(&sb, "{\"type\":\"object\",\"properties\":{");
    for (size_t i = 0; i < dec->u.decide_nd.item_len; i++) {
        const DecideItem *it = &dec->u.decide_nd.items[i];
        if (i > 0) sb_append_ch(&sb, ',');
        sb_append_escaped(&sb, it->name, strlen(it->name));
        sb_append_ch(&sb, ':');
        if (it->kind == DECIDE_CHOICE) {
            sb_append(&sb, "{\"kind\":\"choice\",\"options\":[");
            if (it->spec && it->spec->kind == ND_ARRAY) {
                for (size_t j = 0; j < it->spec->u.array.elems.len; j++) {
                    Node *el = it->spec->u.array.elems.items[j];
                    if (j > 0) sb_append_ch(&sb, ',');
                    if (el->kind == ND_SYMBOL || el->kind == ND_STRING) {
                        sb_append_escaped(&sb, el->u.str.text, el->u.str.len);
                    } else {
                        sb_append(&sb, "\"?\"");
                    }
                }
            }
            sb_append(&sb, "]}");
        } else if (it->kind == DECIDE_SCORE) {
            sb_append(&sb, "{\"kind\":\"score\"}");
        } else {
            sb_append(&sb, "{\"kind\":\"prob\"}");
        }
    }
    sb_append(&sb, "}}");
    char *out = arena_strndup(s->arena, sb.data ? sb.data : "{}", sb.len);
    sb_free(&sb);
    return out;
}

/* ---- Flow static graph verification (reachability + bounded cycles) ---- */

static int flow_node_index(const char **names, size_t n, const char *target) {
    for (size_t i = 0; i < n; i++) {
        if (strcmp(names[i], target) == 0) return (int)i;
    }
    return -1;
}

static void verify_flow(Sema *s, const Node *fl) {
    const char *names[256];
    size_t ncnt = 0;

    /* Gather all declared & edge-referenced nodes */
    if (fl->u.flow_nd.start_node) {
        names[ncnt++] = fl->u.flow_nd.start_node;
    }
    for (size_t i = 0; i < fl->u.flow_nd.node_len; i++) {
        const char *nm = fl->u.flow_nd.nodes[i].name;
        if (flow_node_index(names, ncnt, nm) < 0 && ncnt < 256) names[ncnt++] = nm;
        if (fl->u.flow_nd.nodes[i].handler && fl->u.flow_nd.nodes[i].handler->kind == ND_DECIDE) {
            Node *dec = fl->u.flow_nd.nodes[i].handler;
            dec->u.decide_nd.json_schema = build_decide_json_schema(s, dec);
        }
    }
    for (size_t i = 0; i < fl->u.flow_nd.edge_len; i++) {
        const FlowEdgeDecl *e = &fl->u.flow_nd.edges[i];
        if (flow_node_index(names, ncnt, e->from) < 0 && ncnt < 256) names[ncnt++] = e->from;
        if (flow_node_index(names, ncnt, e->to) < 0 && ncnt < 256) names[ncnt++] = e->to;
    }

    if (!fl->u.flow_nd.start_node) {
        diag_error(s->diags, fl->span, "flow '%s' is missing a 'start :node' declaration", fl->u.flow_nd.name);
        return;
    }

    if (fl->u.flow_nd.state_type) {
        StrBuf tmp; sb_init(&tmp);
        emit_type_json_schema(s, fl->u.flow_nd.state_type, &tmp, 0);
        sb_free(&tmp);
    }

    /* Reachability from start */
    bool visited[256] = {0};
    int queue[256];
    size_t qh = 0, qt = 0;
    int start_idx = flow_node_index(names, ncnt, fl->u.flow_nd.start_node);
    if (start_idx >= 0) {
        visited[start_idx] = true;
        queue[qt++] = start_idx;
    }
    while (qh < qt) {
        int u = queue[qh++];
        for (size_t i = 0; i < fl->u.flow_nd.edge_len; i++) {
            const FlowEdgeDecl *e = &fl->u.flow_nd.edges[i];
            if (strcmp(e->from, names[u]) == 0) {
                int v = flow_node_index(names, ncnt, e->to);
                if (v >= 0 && !visited[v]) {
                    visited[v] = true;
                    queue[qt++] = v;
                }
            }
        }
    }
    for (size_t i = 0; i < ncnt; i++) {
        if (!visited[i]) {
            diag_warn(s->diags, fl->span, "unreachable node ':%s' in flow '%s'",
                      names[i], fl->u.flow_nd.name);
        }
    }

    /* Unbounded cycle detection:
       Ignore edges that carry an explicit `max:` bound. If any cycle remains
       composed strictly of unbounded edges, reject at compile time! */
    int color[256] = {0}; /* 0=unvisited, 1=visiting, 2=done */
    int stack_node[256];
    size_t stack_edge[256];
    for (size_t root = 0; root < ncnt; root++) {
        if (color[root] != 0) continue;
        int top = 0;
        stack_node[0] = (int)root;
        stack_edge[0] = 0;
        color[root] = 1;
        while (top >= 0) {
            int u = stack_node[top];
            size_t ei = stack_edge[top];
            bool pushed = false;
            while (ei < fl->u.flow_nd.edge_len) {
                const FlowEdgeDecl *e = &fl->u.flow_nd.edges[ei++];
                if (e->max_bound != NULL) continue; /* bounded edge breaks unbounded cycles */
                if (strcmp(e->from, names[u]) != 0) continue;
                int v = flow_node_index(names, ncnt, e->to);
                if (v < 0) continue;
                if (color[v] == 1) {
                    diag_error(s->diags, e->span,
                               "unbounded cycle detected in flow '%s' at edge ':%s --> :%s'; add 'max: N' to bound the loop",
                               fl->u.flow_nd.name, e->from, e->to);
                } else if (color[v] == 0) {
                    stack_edge[top] = ei;
                    top++;
                    stack_node[top] = v;
                    stack_edge[top] = 0;
                    color[v] = 1;
                    pushed = true;
                    break;
                }
            }
            if (!pushed) {
                color[u] = 2;
                top--;
            }
        }
    }
}

/* ---- Recursive walk for nested decide nodes & tool validation ---- */

static void check_expr_walk(Sema *s, Node *n) {
    if (!n) return;
    if (n->kind == ND_DECIDE) {
        n->u.decide_nd.json_schema = build_decide_json_schema(s, n);
        if (!n->u.decide_nd.using_model) {
            diag_error(s->diags, n->span, "'decide' requires 'using: :model_name'");
        }
        if (n->u.decide_nd.item_len == 0) {
            diag_error(s->diags, n->span, "'decide' block must declare at least one choice, score, or prob");
        }
        return;
    }
    switch (n->kind) {
        case ND_PROGRAM:
        case ND_BLOCK:
            for (size_t i = 0; i < n->u.block.stmts.len; i++) check_expr_walk(s, n->u.block.stmts.items[i]);
            break;
        case ND_DEF:
            for (size_t i = 0; i < n->u.def_nd.body.len; i++) check_expr_walk(s, n->u.def_nd.body.items[i]);
            break;
        case ND_CLASS:
            for (size_t i = 0; i < n->u.class_nd.body.len; i++) check_expr_walk(s, n->u.class_nd.body.items[i]);
            break;
        case ND_TOOL:
            for (size_t i = 0; i < n->u.tool_nd.body.len; i++) check_expr_walk(s, n->u.tool_nd.body.items[i]);
            break;
        case ND_GUARD:
            for (size_t i = 0; i < n->u.guard_nd.hook_len; i++)
                for (size_t j = 0; j < n->u.guard_nd.hooks[i].body.len; j++)
                    check_expr_walk(s, n->u.guard_nd.hooks[i].body.items[j]);
            break;
        case ND_ASSIGN:
            check_expr_walk(s, n->u.assign.val);
            break;
        case ND_IF:
            check_expr_walk(s, n->u.if_nd.cond);
            for (size_t i = 0; i < n->u.if_nd.then_body.len; i++) check_expr_walk(s, n->u.if_nd.then_body.items[i]);
            for (size_t i = 0; i < n->u.if_nd.else_body.len; i++) check_expr_walk(s, n->u.if_nd.else_body.items[i]);
            break;
        case ND_WITHIN:
            for (size_t i = 0; i < n->u.within_nd.body.len; i++) check_expr_walk(s, n->u.within_nd.body.items[i]);
            break;
        case ND_RETRY:
            for (size_t i = 0; i < n->u.retry_nd.body.len; i++) check_expr_walk(s, n->u.retry_nd.body.items[i]);
            break;
        default:
            break;
    }
}

bool sema_analyze(Node *program, Arena *arena, DiagList *diags, SemaInfo *out) {
    memset(out, 0, sizeof(*out));
    Sema s = { .arena = arena, .diags = diags, .info = out };

    Node *schemas[256], *tools[256], *guards[256], *agents[256], *flows[256], *mcps[64], *a2as[64];
    BindingReq binds[256];
    size_t sl = 0, tl = 0, gl = 0, al = 0, fl = 0, ml = 0, a2l = 0, bl = 0;

    /* Pass 1: Collect top-level declarations */
    for (size_t i = 0; i < program->u.block.stmts.len; i++) {
        Node *st = program->u.block.stmts.items[i];
        switch (st->kind) {
            case ND_USE:
                if (!is_valid_binding_kind(st->u.use_nd.kind)) {
                    diag_error(diags, st->span, "unknown resource binding kind '%s' (expected llm, s1, embedder, db, vector_db, mcp, or skills)",
                               st->u.use_nd.kind);
                } else if (bl < 256) {
                    binds[bl++] = (BindingReq){ .kind = st->u.use_nd.kind, .name = st->u.use_nd.name, .span = st->span };
                }
                break;
            case ND_EXPECT_CONFIG:
                for (size_t j = 0; j < st->u.expect_nd.entry_len; j++) {
                    const ExpectEntry *ent = &st->u.expect_nd.entries[j];
                    if (ent->resource && ent->resource->kind == ND_INDEX &&
                        ent->resource->u.idx.obj && ent->resource->u.idx.obj->kind == ND_GVAR) {
                        const char *k = ent->resource->u.idx.obj->u.ident.name;
                        const char *nm = (ent->resource->u.idx.idx &&
                                          (ent->resource->u.idx.idx->kind == ND_SYMBOL || ent->resource->u.idx.idx->kind == ND_STRING))
                                             ? ent->resource->u.idx.idx->u.str.text : "*";
                        if (!is_valid_binding_kind(k)) {
                            diag_error(diags, ent->span, "unknown resource registry '$%s' (expected $llm, $s1, $embedder, $db, $vector_db, $mcp, or $skills)", k);
                        } else if (bl < 256) {
                            binds[bl++] = (BindingReq){ .kind = k, .name = nm, .span = ent->span };
                        }
                    }
                }
                break;
            case ND_SCHEMA:
                if (sl < 256) schemas[sl++] = st;
                break;
            case ND_TOOL:
                if (tl < 256) tools[tl++] = st;
                break;
            case ND_GUARD:
                if (gl < 256) guards[gl++] = st;
                break;
            case ND_AGENT:
                if (al < 256) agents[al++] = st;
                break;
            case ND_FLOW:
                if (fl < 256) flows[fl++] = st;
                break;
            case ND_MCP_SERVER:
                if (ml < 64) mcps[ml++] = st;
                break;
            case ND_A2A_AGENT:
                if (a2l < 64) a2as[a2l++] = st;
                break;
            default:
                break;
        }
    }

    out->schemas = (Node **)arena_alloc(arena, (sl ? sl : 1) * sizeof(Node *));
    if (sl) memcpy(out->schemas, schemas, sl * sizeof(Node *));
    out->schema_len = sl;

    out->tools = (Node **)arena_alloc(arena, (tl ? tl : 1) * sizeof(Node *));
    if (tl) memcpy(out->tools, tools, tl * sizeof(Node *));
    out->tool_len = tl;

    out->guards = (Node **)arena_alloc(arena, (gl ? gl : 1) * sizeof(Node *));
    if (gl) memcpy(out->guards, guards, gl * sizeof(Node *));
    out->guard_len = gl;

    out->agents = (Node **)arena_alloc(arena, (al ? al : 1) * sizeof(Node *));
    if (al) memcpy(out->agents, agents, al * sizeof(Node *));
    out->agent_len = al;

    out->flows = (Node **)arena_alloc(arena, (fl ? fl : 1) * sizeof(Node *));
    if (fl) memcpy(out->flows, flows, fl * sizeof(Node *));
    out->flow_len = fl;

    out->mcp_servers = (Node **)arena_alloc(arena, (ml ? ml : 1) * sizeof(Node *));
    if (ml) memcpy(out->mcp_servers, mcps, ml * sizeof(Node *));
    out->mcp_len = ml;

    out->a2a_agents = (Node **)arena_alloc(arena, (a2l ? a2l : 1) * sizeof(Node *));
    if (a2l) memcpy(out->a2a_agents, a2as, a2l * sizeof(Node *));
    out->a2a_len = a2l;

    out->bindings = (BindingReq *)arena_alloc(arena, (bl ? bl : 1) * sizeof(BindingReq));
    if (bl) memcpy(out->bindings, binds, bl * sizeof(BindingReq));
    out->binding_len = bl;

    /* Pass 2: Build JSON schemas & validate */
    for (size_t i = 0; i < sl; i++) {
        Node *sc = out->schemas[i];
        sc->u.schema_nd.json_schema = build_fields_json_schema(&s, sc->u.schema_nd.fields, sc->u.schema_nd.field_len);
    }

    for (size_t i = 0; i < tl; i++) {
        Node *t = out->tools[i];
        t->u.tool_nd.json_schema = build_fields_json_schema(&s, t->u.tool_nd.params, t->u.tool_nd.param_len);
        if (t->u.tool_nd.ret_type) {
            StrBuf tmp; sb_init(&tmp);
            emit_type_json_schema(&s, t->u.tool_nd.ret_type, &tmp, 0);
            sb_free(&tmp);
        }
    }

    for (size_t i = 0; i < ml; i++) {
        Node *m = out->mcp_servers[i];
        for (size_t j = 0; j < m->u.mcp_nd.exposed_len; j++) {
            const char *tname = m->u.mcp_nd.exposed[j];
            if (!find_tool(&s, tname)) {
                diag_error(diags, m->span, "mcp_server '%s' exposes unknown tool ':%s'",
                           m->u.mcp_nd.name, tname);
            }
        }
        for (size_t j = 0; j < m->u.mcp_nd.inline_tools.len; j++) {
            Node *it = m->u.mcp_nd.inline_tools.items[j];
            it->u.tool_nd.json_schema = build_fields_json_schema(&s, it->u.tool_nd.params, it->u.tool_nd.param_len);
            if (it->u.tool_nd.ret_type) {
                StrBuf tmp; sb_init(&tmp);
                emit_type_json_schema(&s, it->u.tool_nd.ret_type, &tmp, 0);
                sb_free(&tmp);
            }
        }
    }

    for (size_t i = 0; i < fl; i++) {
        verify_flow(&s, out->flows[i]);
    }

    for (size_t i = 0; i < al; i++) {
        Node *ag = out->agents[i];
        if (!ag->u.agent_nd.model) {
            diag_warn(diags, ag->span, "agent '%s' does not declare a 'model :name'", ag->u.agent_nd.name);
        }
    }

    check_expr_walk(&s, program);
    return diags->error_count == 0;
}
