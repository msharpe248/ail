#ifndef AIL_CODEGEN_H
#define AIL_CODEGEN_H

#include "ast.h"
#include "sema.h"
#include "util.h"

bool codegen_module(const char *source_file, Node *program, const SemaInfo *sema,
                    Arena *arena, DiagList *diags, StrBuf *out_masm);

#endif
