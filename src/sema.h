#ifndef AIL_SEMA_H
#define AIL_SEMA_H

#include "ast.h"
#include "util.h"

typedef struct {
    const char *kind;
    const char *name;
    Span span;
} BindingReq;

typedef struct {
    Node **schemas;
    size_t schema_len;
    Node **tools;
    size_t tool_len;
    Node **guards;
    size_t guard_len;
    Node **agents;
    size_t agent_len;
    Node **flows;
    size_t flow_len;
    Node **mcp_servers;
    size_t mcp_len;
    Node **a2a_agents;
    size_t a2a_len;
    BindingReq *bindings;
    size_t binding_len;
} SemaInfo;

bool sema_analyze(Node *program, Arena *arena, DiagList *diags, SemaInfo *out);

#endif
