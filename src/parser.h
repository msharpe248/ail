#ifndef AIL_PARSER_H
#define AIL_PARSER_H

#include "ast.h"
#include "lexer.h"
#include "util.h"

Node *parse_program(const TokenList *tokens, Arena *arena, DiagList *diags);

#endif
