#include "ast.h"
#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"
#include "util.h"

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define AIL_VERSION "0.5.0"

static volatile pid_t g_child_pid = 0;

static void forward_signal(int sig) {
    if (g_child_pid > 0) kill(g_child_pid, sig);
}

static void usage(void) {
    fprintf(stderr,
        "ail %s — AI Language compiler targeting MLWSVM 0.30.0\n\n"
        "Usage:\n"
        "  ail check  <file.ail>                     Lex, parse, verify schemas & flow graphs\n"
        "  ail build  <file.ail> [-o out.masm]       Compile to MLWSVM assembly (.masm)\n"
        "  ail run    <file.ail> [--env FILE] [-- <args...>]\n"
        "                                            Compile and execute on mlwsvm\n"
        "  ail debug  <file.ail> [--env FILE]        Launch mlwsvm debugger with .loc source info\n"
        "  ail repl   [--env FILE]                   Start interactive AIL REPL\n"
        "  ail bundle <file.ail> -o <bin>            Bundle into a standalone executable\n"
        "  ail version                               Print version\n",
        AIL_VERSION);
}

static bool file_is_executable(const char *path) {
    return path && access(path, X_OK) == 0;
}

static const char *find_mlwsvm(void) {
    const char *env = getenv("MLWSVM");
    if (env && env[0]) return env;
    if (file_is_executable("../mlws-vm/mlwsvm")) return "../mlws-vm/mlwsvm";
    if (file_is_executable("./mlwsvm")) return "./mlwsvm";
    return "mlwsvm";
}

static const char *find_runtime_dir(void) {
    const char *env = getenv("AIL_RUNTIME");
    if (env && env[0]) return env;
    struct stat st;
    if (stat("runtime/ail/core.masm", &st) == 0) return "runtime";
    return "runtime";
}

/* ---- Multi-file `require` loader with cycle detection & deduplication ---- */

typedef struct {
    char *loaded[256];
    size_t loaded_len;
    char *active[64];
    size_t active_len;
} RequireState;

static void resolve_req_path(const char *from_file, const char *req, char *out, size_t cap) {
    char combined[PATH_MAX];
    if (req[0] == '/') {
        snprintf(combined, sizeof(combined), "%s", req);
    } else {
        const char *slash = from_file ? strrchr(from_file, '/') : NULL;
        if (slash) {
            size_t dlen = (size_t)(slash - from_file);
            if (dlen >= sizeof(combined) - 2) dlen = sizeof(combined) - 2;
            memcpy(combined, from_file, dlen);
            combined[dlen] = '/';
            snprintf(combined + dlen + 1, sizeof(combined) - dlen - 1, "%s", req);
        } else {
            snprintf(combined, sizeof(combined), "%s", req);
        }
    }
    size_t clen = strlen(combined);
    if (clen < 4 || strcmp(combined + clen - 4, ".ail") != 0) {
        snprintf(out, cap, "%s.ail", combined);
    } else {
        snprintf(out, cap, "%s", combined);
    }
}

static bool str_in_list(char *const *list, size_t n, const char *s) {
    for (size_t i = 0; i < n; i++) {
        if (strcmp(list[i], s) == 0) return true;
    }
    return false;
}

static Node *parse_and_expand_file(const char *path, Arena *arena, DiagList *diags, RequireState *rs, Span from_span) {
    char realbuf[PATH_MAX];
    const char *canon = realpath(path, realbuf) ? realbuf : path;

    if (str_in_list(rs->active, rs->active_len, canon)) {
        diag_error(diags, from_span, "circular require detected involving '%s'", path);
        return NULL;
    }
    if (str_in_list(rs->loaded, rs->loaded_len, canon)) {
        return node_new(arena, ND_PROGRAM, from_span); /* already loaded (diamond dedup) */
    }

    size_t len = 0;
    char *src = read_entire_file(path, &len);
    if (!src) {
        diag_error(diags, from_span, "cannot require file '%s'", path);
        return NULL;
    }

    char *canon_copy = arena_strdup(arena, canon);
    char *disp_copy = arena_strdup(arena, path);
    if (rs->active_len < 64) rs->active[rs->active_len++] = canon_copy;

    TokenList toks;
    lex_source(disp_copy, src, len, arena, diags, &toks);
    Node *prog = NULL;
    if (diags->error_count == 0) {
        prog = parse_program(&toks, arena, diags);
    }
    token_list_free(&toks);
    free(src);

    if (prog && diags->error_count == 0) {
        NodeList expanded = {0};
        for (size_t i = 0; i < prog->u.block.stmts.len; i++) {
            Node *st = prog->u.block.stmts.items[i];
            if (st->kind == ND_REQUIRE) {
                char target[PATH_MAX];
                resolve_req_path(disp_copy, st->u.str.text, target, sizeof(target));
                Node *sub = parse_and_expand_file(target, arena, diags, rs, st->span);
                if (sub) {
                    for (size_t j = 0; j < sub->u.block.stmts.len; j++) {
                        nodelist_push(arena, &expanded, sub->u.block.stmts.items[j]);
                    }
                }
            } else {
                nodelist_push(arena, &expanded, st);
            }
        }
        prog->u.block.stmts = expanded;
    }

    if (rs->active_len > 0) rs->active_len--;
    if (rs->loaded_len < 256) rs->loaded[rs->loaded_len++] = canon_copy;
    return prog;
}

static bool compile_source_str(const char *display_name, const char *src, size_t len,
                               bool emit_code, StrBuf *out_masm, SemaInfo *out_sema) {
    Arena arena;
    arena_init(&arena);
    DiagList diags;
    diag_init(&diags);

    TokenList toks;
    lex_source(display_name, src, len, &arena, &diags, &toks);
    Node *prog = NULL;
    if (diags.error_count == 0) {
        prog = parse_program(&toks, &arena, &diags);
    }
    token_list_free(&toks);

    /* Expand any `require` statements from REPL/string sources relative to cwd */
    if (prog && diags.error_count == 0) {
        RequireState rs = {0};
        NodeList expanded = {0};
        for (size_t i = 0; i < prog->u.block.stmts.len; i++) {
            Node *st = prog->u.block.stmts.items[i];
            if (st->kind == ND_REQUIRE) {
                char target[PATH_MAX];
                resolve_req_path(NULL, st->u.str.text, target, sizeof(target));
                Node *sub = parse_and_expand_file(target, &arena, &diags, &rs, st->span);
                if (sub) {
                    for (size_t j = 0; j < sub->u.block.stmts.len; j++) {
                        nodelist_push(&arena, &expanded, sub->u.block.stmts.items[j]);
                    }
                }
            } else {
                nodelist_push(&arena, &expanded, st);
            }
        }
        prog->u.block.stmts = expanded;
    }

    SemaInfo sema;
    memset(&sema, 0, sizeof(sema));
    if (diags.error_count == 0 && prog) {
        sema_analyze(prog, &arena, &diags, &sema);
    }
    if (diags.error_count == 0 && emit_code && out_masm && prog) {
        codegen_module(display_name, prog, &sema, &arena, &diags, out_masm);
    }

    diag_print(&diags);
    bool ok = (diags.error_count == 0);
    if (out_sema) {
        memset(out_sema, 0, sizeof(*out_sema));
        out_sema->schema_len = sema.schema_len;
        out_sema->tool_len = sema.tool_len;
        out_sema->guard_len = sema.guard_len;
        out_sema->agent_len = sema.agent_len;
        out_sema->flow_len = sema.flow_len;
        out_sema->mcp_len = sema.mcp_len;
        out_sema->a2a_len = sema.a2a_len;
        out_sema->binding_len = sema.binding_len;
    }

    diag_free(&diags);
    arena_free(&arena);
    return ok;
}

static bool compile_file(const char *in_path, bool emit_code, StrBuf *out_masm, SemaInfo *out_sema) {
    Arena arena;
    arena_init(&arena);
    DiagList diags;
    diag_init(&diags);

    RequireState rs = {0};
    Span root_sp = { .file = in_path, .line = 1, .col = 1 };
    Node *prog = parse_and_expand_file(in_path, &arena, &diags, &rs, root_sp);

    SemaInfo sema;
    memset(&sema, 0, sizeof(sema));
    if (diags.error_count == 0 && prog) {
        sema_analyze(prog, &arena, &diags, &sema);
    }
    if (diags.error_count == 0 && emit_code && out_masm && prog) {
        codegen_module(in_path, prog, &sema, &arena, &diags, out_masm);
    }

    diag_print(&diags);
    bool ok = (diags.error_count == 0);
    if (out_sema) {
        memset(out_sema, 0, sizeof(*out_sema));
        out_sema->schema_len = sema.schema_len;
        out_sema->tool_len = sema.tool_len;
        out_sema->guard_len = sema.guard_len;
        out_sema->agent_len = sema.agent_len;
        out_sema->flow_len = sema.flow_len;
        out_sema->mcp_len = sema.mcp_len;
        out_sema->a2a_len = sema.a2a_len;
        out_sema->binding_len = sema.binding_len;
    }

    diag_free(&diags);
    arena_free(&arena);
    return ok;
}

static int run_exec(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "ail: failed to execute '%s'\n", argv[0]);
        _exit(127);
    }
    g_child_pid = pid;
    void (*old_int)(int)  = signal(SIGINT, forward_signal);
    void (*old_term)(int) = signal(SIGTERM, forward_signal);
    void (*old_hup)(int)  = signal(SIGHUP, forward_signal);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        /* retry if interrupted by forwarded signal */
    }
    g_child_pid = 0;
    signal(SIGINT, old_int);
    signal(SIGTERM, old_term);
    signal(SIGHUP, old_hup);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

/* ---- Interactive REPL (`ail repl [--env FILE]`) ---- */

static int repl_block_delta(const char *line) {
    /* Rough balanced opener/closer heuristic for multiline REPL input */
    Arena a; arena_init(&a);
    DiagList dl; diag_init(&dl);
    TokenList tl;
    lex_source("<repl>", line, strlen(line), &a, &dl, &tl);
    int delta = 0;
    for (size_t i = 0; i < tl.len; i++) {
        switch (tl.items[i].kind) {
            case TOK_DEF: case TOK_CLASS: case TOK_MODULE:
            case TOK_IF: case TOK_UNLESS: case TOK_CASE:
            case TOK_WHILE: case TOK_UNTIL: case TOK_BEGIN:
            case TOK_DO: case TOK_SCHEMA: case TOK_FLOW:
                delta++;
                break;
            case TOK_END:
                delta--;
                break;
            default:
                break;
        }
    }
    token_list_free(&tl);
    diag_free(&dl);
    arena_free(&a);
    return delta;
}

static bool repl_is_decl_or_assign(const char *src) {
    const char *p = src;
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    static const char *prefixes[] = {
        "def ", "class ", "module ", "use ", "require ", "require_relative ",
        "schema ", "tool ", "guard ", "skill ", "agent ", "flow "
    };
    for (size_t i = 0; i < sizeof(prefixes)/sizeof(prefixes[0]); i++) {
        if (strncmp(p, prefixes[i], strlen(prefixes[i])) == 0) return true;
    }
    /* Check simple assignment `ident = ...` (not `==`) */
    const char *q = p;
    if ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || *q == '_') {
        while ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') ||
               (*q >= '0' && *q <= '9') || *q == '_') q++;
        while (*q == ' ' || *q == '\t') q++;
        if (q[0] == '=' && q[1] != '=' && q[1] != '>' && q[1] != '~') return true;
        if ((q[0] == '+' || q[0] == '-' || q[0] == '*' || q[0] == '/') && q[1] == '=') return true;
    }
    return false;
}

static int run_repl(const char *env_file) {
    bool is_tty = isatty(STDIN_FILENO);
    if (is_tty) {
        printf("AIL %s REPL (target: MLWSVM 0.30.0) — type :help or :quit\n", AIL_VERSION);
    }

    StrBuf history;
    sb_init(&history);
    StrBuf pending;
    sb_init(&pending);
    int depth = 0;
    char line[4096];
    const char *vm = find_mlwsvm();
    const char *rt = find_runtime_dir();

    for (;;) {
        if (is_tty) {
            printf("%s", depth > 0 ? "..> " : "ail> ");
            fflush(stdout);
        }
        if (!fgets(line, sizeof(line), stdin)) break;

        /* Trim trailing newline for meta-commands */
        size_t llen = strlen(line);
        while (llen > 0 && (line[llen - 1] == '\n' || line[llen - 1] == '\r')) line[--llen] = '\0';

        if (depth == 0) {
            if (llen == 0) continue;
            if (strcmp(line, ":quit") == 0 || strcmp(line, ":exit") == 0 || strcmp(line, "exit") == 0) break;
            if (strcmp(line, ":help") == 0) {
                printf("Commands: :help  :defs  :reset  :quit\n");
                continue;
            }
            if (strcmp(line, ":reset") == 0) {
                sb_free(&history); sb_init(&history);
                printf("REPL state reset.\n");
                continue;
            }
            if (strcmp(line, ":defs") == 0) {
                printf("%s", history.len ? history.data : "(empty)\n");
                continue;
            }
        }

        sb_append(&pending, line);
        sb_append_ch(&pending, '\n');
        depth += repl_block_delta(line);
        if (depth > 0) continue;
        depth = 0;

        bool persist = repl_is_decl_or_assign(pending.data);
        StrBuf prog;
        sb_init(&prog);
        if (history.len > 0) sb_append_n(&prog, history.data, history.len);
        if (persist) {
            sb_append_n(&prog, pending.data, pending.len);
            sb_append(&prog, "puts \"=> ok\"\n");
        } else {
            sb_append(&prog, "__ail_repl_res = (\n");
            sb_append_n(&prog, pending.data, pending.len);
            sb_append(&prog, ")\nputs \"=> #{__ail_repl_res.inspect}\"\n");
        }

        StrBuf masm;
        sb_init(&masm);
        if (compile_source_str("<repl>", prog.data, prog.len, true, &masm, NULL)) {
            char tmp_path[128];
            snprintf(tmp_path, sizeof(tmp_path), "/tmp/ail_repl_%d.masm", (int)getpid());
            if (write_entire_file(tmp_path, masm.data, masm.len)) {
                char *vargv[16];
                int k = 0;
                vargv[k++] = (char *)vm;
                vargv[k++] = "run";
                vargv[k++] = "-L";
                vargv[k++] = (char *)rt;
                if (env_file) {
                    vargv[k++] = "--env";
                    vargv[k++] = (char *)env_file;
                }
                vargv[k++] = tmp_path;
                vargv[k] = NULL;
                int rc = run_exec(vargv);
                unlink(tmp_path);
                if (rc == 0 && persist) {
                    sb_append_n(&history, pending.data, pending.len);
                }
            }
        }
        sb_free(&masm);
        sb_free(&prog);
        sb_free(&pending);
        sb_init(&pending);
    }

    sb_free(&pending);
    sb_free(&history);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    const char *cmd = argv[1];
    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0) {
        printf("ail %s\n", AIL_VERSION);
        return 0;
    }
    if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        usage();
        return 0;
    }

    if (strcmp(cmd, "repl") == 0) {
        const char *env_file = NULL;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--env") == 0 && i + 1 < argc) env_file = argv[++i];
        }
        return run_repl(env_file);
    }

    if (strcmp(cmd, "check") == 0) {
        if (argc < 3) { usage(); return 1; }
        SemaInfo info;
        if (!compile_file(argv[2], false, NULL, &info)) return 1;
        printf("ok: %s (bindings=%zu, schemas=%zu, tools=%zu, guards=%zu, agents=%zu, flows=%zu, mcp=%zu, a2a=%zu)\n",
               argv[2], info.binding_len, info.schema_len, info.tool_len, info.guard_len,
               info.agent_len, info.flow_len, info.mcp_len, info.a2a_len);
        return 0;
    }

    if (strcmp(cmd, "build") == 0) {
        if (argc < 3) { usage(); return 1; }
        const char *in_path = argv[2];
        const char *out_path = NULL;
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out_path = argv[++i];
        }
        StrBuf masm;
        sb_init(&masm);
        if (!compile_file(in_path, true, &masm, NULL)) {
            sb_free(&masm);
            return 1;
        }
        if (out_path) {
            if (!write_entire_file(out_path, masm.data, masm.len)) {
                fprintf(stderr, "ail: failed to write '%s'\n", out_path);
                sb_free(&masm);
                return 1;
            }
        } else {
            fwrite(masm.data, 1, masm.len, stdout);
        }
        sb_free(&masm);
        return 0;
    }

    if (strcmp(cmd, "run") == 0 || strcmp(cmd, "debug") == 0) {
        if (argc < 3) { usage(); return 1; }
        bool is_debug = (strcmp(cmd, "debug") == 0);
        const char *in_path = argv[2];
        const char *env_file = NULL;
        int prog_args_start = argc;
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--") == 0) {
                prog_args_start = i + 1;
                break;
            }
            if (strcmp(argv[i], "--env") == 0 && i + 1 < argc) {
                env_file = argv[++i];
            } else {
                prog_args_start = i;
                break;
            }
        }

        StrBuf masm;
        sb_init(&masm);
        if (!compile_file(in_path, true, &masm, NULL)) {
            sb_free(&masm);
            return 1;
        }

        char tmp_path[128];
        snprintf(tmp_path, sizeof(tmp_path), "/tmp/ail_%d.masm", (int)getpid());
        if (!write_entire_file(tmp_path, masm.data, masm.len)) {
            fprintf(stderr, "ail: failed to write temp file '%s'\n", tmp_path);
            sb_free(&masm);
            return 1;
        }
        sb_free(&masm);

        const char *rt = find_runtime_dir();
        const char *vm = find_mlwsvm();

        char *vargv[128];
        int k = 0;
        vargv[k++] = (char *)vm;
        vargv[k++] = is_debug ? "debug" : "run";
        vargv[k++] = "-L";
        vargv[k++] = (char *)rt;
        if (env_file) {
            vargv[k++] = "--env";
            vargv[k++] = (char *)env_file;
            setenv("AIL_ENV_FILE", env_file, 1);
        }
        vargv[k++] = tmp_path;
        for (int i = prog_args_start; i < argc && k < 126; i++) {
            vargv[k++] = argv[i];
        }
        vargv[k] = NULL;

        int rc = run_exec(vargv);
        unlink(tmp_path);
        return rc;
    }

    if (strcmp(cmd, "bundle") == 0) {
        if (argc < 3) { usage(); return 1; }
        const char *in_path = NULL;
        const char *out_bin = NULL;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out_bin = argv[++i];
            else if (!in_path) in_path = argv[i];
        }
        if (!in_path || !out_bin) { usage(); return 1; }

        StrBuf masm;
        sb_init(&masm);
        if (!compile_file(in_path, true, &masm, NULL)) {
            sb_free(&masm);
            return 1;
        }

        char tmp_path[128];
        snprintf(tmp_path, sizeof(tmp_path), "/tmp/ail_bundle_%d.masm", (int)getpid());
        if (!write_entire_file(tmp_path, masm.data, masm.len)) {
            sb_free(&masm);
            return 1;
        }
        sb_free(&masm);

        const char *rt = find_runtime_dir();
        const char *vm = find_mlwsvm();

        char *vargv[16];
        int k = 0;
        vargv[k++] = (char *)vm;
        vargv[k++] = "bundle";
        vargv[k++] = "-L";
        vargv[k++] = (char *)rt;
        vargv[k++] = "-o";
        vargv[k++] = (char *)out_bin;
        vargv[k++] = "--opt";
        vargv[k++] = "2";
        vargv[k++] = tmp_path;
        vargv[k] = NULL;

        int rc = run_exec(vargv);
        unlink(tmp_path);
        return rc;
    }

    usage();
    return 1;
}
