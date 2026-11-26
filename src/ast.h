#ifndef AIL_AST_H
#define AIL_AST_H

#include "lexer.h"
#include "util.h"

typedef struct Node Node;
typedef struct TypeExpr TypeExpr;

typedef struct {
    Node **items;
    size_t len;
    size_t cap;
} NodeList;

typedef struct {
    const char *key;
    Node *val;
    Span span;
} KvPair;

typedef struct {
    KvPair *items;
    size_t len;
    size_t cap;
} KvList;

typedef struct {
    const char *name;
    TypeExpr *type;
    Node *default_val;
    Span span;
} Param;

typedef struct {
    Param *items;
    size_t len;
    size_t cap;
} ParamList;

/* ---- Boundary type expressions ---- */

typedef enum {
    TY_NAMED,      /* String, Int, Match */
    TY_GENERIC,    /* Array[T], Hash[K, V] */
    TY_OPTIONAL,   /* T? */
    TY_ENUM        /* Enum[:a, :b] */
} TypeKind;

struct TypeExpr {
    TypeKind kind;
    Span span;
    const char *name;
    TypeExpr **args;
    size_t arg_len;
    const char **enum_vals;
    size_t enum_len;
};

/* ---- AI Construct sub-records ---- */

typedef struct {
    const char *name;
    TypeExpr *type;
    const char *doc;
    Node *default_val;
    Span span;
} FieldDecl;

typedef struct {
    enum { GUARD_INPUT, GUARD_OUTPUT, GUARD_TOOL } kind;
    ParamList params;
    NodeList body;
    Span span;
} GuardHook;

typedef struct {
    enum { DECIDE_CHOICE, DECIDE_SCORE, DECIDE_PROB } kind;
    const char *name;
    Node *spec; /* choices array or range, NULL for prob */
    Span span;
} DecideItem;

typedef struct {
    const char *name;
    Node *handler; /* NULL, callable expr, or ND_DECIDE */
    Span span;
} FlowNodeDecl;

typedef struct {
    const char *from;
    const char *to;
    Node *cond;      /* when: / unless: expr */
    bool invert_cond;
    Node *max_bound; /* max: expr (required on cycles) */
    Span span;
} FlowEdgeDecl;

typedef struct {
    const char *uri;
    const char *mime;
    NodeList body;
    Span span;
} McpResourceDecl;

typedef struct {
    const char *name;
    const char *doc;
    FieldDecl *params;
    size_t param_len;
    NodeList body;
    Span span;
} McpPromptDecl;

typedef struct {
    const char *exc_type; /* NULL = catch-all */
    const char *var_name; /* NULL = unnamed */
    NodeList body;
    Span span;
} RescueClause;

typedef struct {
    NodeList patterns;
    NodeList body;
    Span span;
} WhenClause;

/* ---- Unified AST Node ---- */

typedef enum {
    ND_PROGRAM,
    ND_BLOCK,

    /* Literals */
    ND_NIL,
    ND_BOOL,
    ND_INT,
    ND_FLOAT,
    ND_DECIMAL,
    ND_STRING,
    ND_INTERP,
    ND_SYMBOL,
    ND_REGEX,
    ND_ARRAY,
    ND_HASH,
    ND_RANGE,

    /* Variables & ops */
    ND_IDENT,
    ND_IVAR,
    ND_SELF,
    ND_CONST_PATH,
    ND_ASSIGN,
    ND_INDEX_ASSIGN,
    ND_ATTR_ASSIGN,
    ND_UNARY,
    ND_BINARY,
    ND_INDEX,

    /* Calls & lambdas */
    ND_CALL,
    ND_LAMBDA,
    ND_YIELD,

    /* Control flow */
    ND_IF,
    ND_CASE,
    ND_WHILE,
    ND_RETURN,
    ND_BREAK,
    ND_NEXT,
    ND_BEGIN,
    ND_RAISE,

    /* Definitions */
    ND_DEF,
    ND_CLASS,
    ND_MODULE,
    ND_INCLUDE,

    /* First-class AIL constructs */
    ND_REQUIRE,
    ND_USE,
    ND_SCHEMA,
    ND_TOOL,
    ND_GUARD,
    ND_SKILL,
    ND_DECIDE,
    ND_AGENT,
    ND_FLOW,
    ND_WITHIN,
    ND_RETRY,
    ND_MCP_SERVER,
    ND_A2A_AGENT
} NodeKind;

struct Node {
    NodeKind kind;
    Span span;

    union {
        struct { NodeList stmts; } block;
        struct { bool val; } bval;
        struct { int64_t val; } ival;
        struct { double val; } fval;
        struct { const char *text; size_t len; } str;
        struct { NodeList parts; } interp;
        struct { const char *pattern; const char *flags; } regex;
        struct { NodeList elems; } array;
        struct { NodeList keys; NodeList vals; } hash;
        struct { Node *start; Node *stop; bool excl; } range;

        struct { const char *name; } ident;
        struct { Node *base; const char *name; } const_path;
        struct { const char *name; bool is_ivar; TokKind op; Node *val; } assign;
        struct { Node *obj; Node *idx; TokKind op; Node *val; } index_assign;
        struct { Node *obj; const char *attr; TokKind op; Node *val; } attr_assign;

        struct { TokKind op; Node *expr; } unary;
        struct { TokKind op; Node *lhs; Node *rhs; } binary;
        struct { Node *obj; Node *idx; } idx;

        struct {
            Node *recv;       /* NULL for bare call */
            const char *name;
            NodeList args;
            KvList kwargs;
            bool has_block;
            ParamList block_params;
            NodeList block_body;
        } call;

        struct { ParamList params; NodeList body; } lambda;
        struct { NodeList args; } yield_nd;

        struct {
            Node *cond;
            bool invert;      /* unless */
            NodeList then_body;
            NodeList else_body; /* may hold single ND_IF for elsif */
        } if_nd;

        struct {
            Node *expr;
            WhenClause *whens;
            size_t when_len;
            NodeList else_body;
        } case_nd;

        struct { Node *cond; bool invert; NodeList body; } while_nd;
        struct { Node *val; } ret;
        struct { Node *exc; Node *msg; } raise_nd;

        struct {
            NodeList body;
            RescueClause *rescues;
            size_t rescue_len;
            NodeList ensure_body;
        } begin_nd;

        struct {
            const char *name;
            ParamList params;
            TypeExpr *ret_type;
            NodeList body;
        } def_nd;

        struct {
            const char *name;
            const char *super_name;
            NodeList body;
        } class_nd;

        struct { const char *name; NodeList body; } mod_nd;
        struct { const char *name; } inc_nd;

        /* ---- AIL nodes ---- */
        struct {
            const char *kind; /* "llm", "s1", "embedder", "db", "vector_db", "mcp", "skills" */
            const char *name;
            KvList opts;
        } use_nd;

        struct {
            const char *name;
            FieldDecl *fields;
            size_t field_len;
            char *json_schema; /* populated by sema */
        } schema_nd;

        struct {
            const char *name;
            const char *doc;
            FieldDecl *params;
            size_t param_len;
            TypeExpr *ret_type;
            KvList capabilities;
            KvList limits;
            NodeList body;
            char *json_schema; /* populated by sema */
        } tool_nd;

        struct {
            const char *name;
            GuardHook *hooks;
            size_t hook_len;
            KvList capabilities;
        } guard_nd;

        struct {
            const char *name;
            const char *doc;
            Node *tools;
            KvList capabilities;
            Node *instructions;
            NodeList body;
        } skill_nd;

        struct {
            const char *node_name; /* optional when used inside flow */
            Node *using_model;
            Node *on_expr;
            DecideItem *items;
            size_t item_len;
            char *json_schema;
        } decide_nd;

        struct {
            const char *name;
            Node *model;
            Node *context_view;
            Node *tools;
            Node *mcp;
            Node *skills;
            Node *guards;
            Node *system_prompt;
            TypeExpr *ret_type;
            KvList capabilities;
            KvList limits;
        } agent_nd;

        struct {
            const char *name;
            TypeExpr *state_type;
            const char *start_node;
            FlowNodeDecl *nodes;
            size_t node_len;
            FlowEdgeDecl *edges;
            size_t edge_len;
        } flow_nd;

        struct {
            Node *spec; /* e.g. budget(...) call */
            NodeList body;
            RescueClause *rescues;
            size_t rescue_len;
            NodeList ensure_body;
        } within_nd;

        struct {
            Node *count;
            KvList opts;
            const char *attempt_var;
            NodeList body;
        } retry_nd;

        struct {
            const char *name;
            const char *version;
            KvList capabilities;
            const char **exposed;
            size_t exposed_len;
            NodeList inline_tools;
            McpResourceDecl *resources;
            size_t resource_len;
            McpPromptDecl *prompts;
            size_t prompt_len;
        } mcp_nd;

        struct {
            const char *name;
            const char *version;
            Node *description;
            Node *handler;
            KvList capabilities;
            KvList limits;
        } a2a_nd;
    } u;
};

Node *node_new(Arena *a, NodeKind kind, Span sp);
void nodelist_push(Arena *a, NodeList *nl, Node *n);
void kvlist_push(Arena *a, KvList *kl, const char *k, Node *v, Span sp);
void paramlist_push(Arena *a, ParamList *pl, Param p);

#endif
