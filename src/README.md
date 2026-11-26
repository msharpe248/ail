# AIL Compiler Source (`src/`)

The AIL compiler is written in **plain C11** (`-std=c11 -Wall -Wextra -Werror`) with **zero external dependencies**.

## Compilation Pipeline

```text
  .ail files
      │
      ▼
  lexer.c       Tokenization (Ruby-style syntax, string interpolation, heredocs,
      │         decimals `0.25d`, durations `30.s`, symbols `:foo`, regex `/.../`)
      ▼
  parser.c      Recursive-descent parser producing AST nodes (ast.h / ast.c)
      │
      ▼
  main.c        Recursive `require` / `require_relative` expansion with `realpath()`
      │         diamond deduplication and circular-import rejection
      ▼
  sema.c        Static semantic checks: schema definitions, `use` resource bindings,
      │         flow node reachability, and compile-time unbounded-cycle rejection
      ▼
  codegen.c     Emits MLWSVM 0.30.0 assembly (.masm) with `.loc` multi-file source
      │         mapping, `.debug_format core.debug_format`, and runtime calls
      ▼
  main.c        CLI driver (`check`, `build`, `run`, `debug`, `repl`, `bundle`, `version`)
                and signal-forwarding launcher for `mlwsvm`
```

## Files

| File | Role |
|---|---|
| [`util.h`](util.h) / [`util.c`](util.c) | Dynamic string buffers (`StrBuf`), arena/helpers, file I/O, diagnostics |
| [`lexer.h`](lexer.h) / [`lexer.c`](lexer.c) | Lexer and token definitions |
| [`ast.h`](ast.h) / [`ast.c`](ast.c) | AST node definitions and constructors |
| [`parser.h`](parser.h) / [`parser.c`](parser.c) | Parser for Ruby-like core + all AI constructs |
| [`sema.h`](sema.h) / [`sema.c`](sema.c) | Static verifier (schemas, resource bindings, flow graph cycle & reachability checks) |
| [`codegen.h`](codegen.h) / [`codegen.c`](codegen.c) | MLWSVM `.masm` code generator |
| [`main.c`](main.c) | CLI entry point, multi-file `require` loader, interactive `ail repl`, `ail debug`, and `mlwsvm` runner |

## Building

```sh
make            # builds ./ail from src/*.c
make clean      # removes build/ and ./ail
```
