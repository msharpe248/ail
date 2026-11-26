#include "ast.h"

#include <string.h>

Node *node_new(Arena *a, NodeKind kind, Span sp) {
    Node *n = (Node *)arena_alloc(a, sizeof(Node));
    n->kind = kind;
    n->span = sp;
    return n;
}

void nodelist_push(Arena *a, NodeList *nl, Node *n) {
    if (nl->len == nl->cap) {
        size_t ncap = nl->cap ? nl->cap * 2 : 8;
        Node **ni = (Node **)arena_alloc(a, ncap * sizeof(Node *));
        if (nl->len > 0) memcpy(ni, nl->items, nl->len * sizeof(Node *));
        nl->items = ni;
        nl->cap = ncap;
    }
    nl->items[nl->len++] = n;
}

void kvlist_push(Arena *a, KvList *kl, const char *k, Node *v, Span sp) {
    if (kl->len == kl->cap) {
        size_t ncap = kl->cap ? kl->cap * 2 : 4;
        KvPair *ni = (KvPair *)arena_alloc(a, ncap * sizeof(KvPair));
        if (kl->len > 0) memcpy(ni, kl->items, kl->len * sizeof(KvPair));
        kl->items = ni;
        kl->cap = ncap;
    }
    kl->items[kl->len++] = (KvPair){ .key = k, .val = v, .span = sp };
}

void paramlist_push(Arena *a, ParamList *pl, Param p) {
    if (pl->len == pl->cap) {
        size_t ncap = pl->cap ? pl->cap * 2 : 4;
        Param *ni = (Param *)arena_alloc(a, ncap * sizeof(Param));
        if (pl->len > 0) memcpy(ni, pl->items, pl->len * sizeof(Param));
        pl->items = ni;
        pl->cap = ncap;
    }
    pl->items[pl->len++] = p;
}
