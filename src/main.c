#include "ast.h"
#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define AIL_VERSION "0.2.0"

static void usage(void) {
    fprintf(stderr,
        "ail %s — AI Language compiler targeting MLWSVM\n\n"
        "Usage:\n"
        "  ail check <file.ail>                     Lex, parse, verify schemas & flow graphs\n"
        "  ail build <file.ail> [-o out.masm]       Compile to MLWSVM assembly (.masm)\n"
        "  ail run   <file.ail> [--env FILE] [-- <args...>]\n"
        "                                           Compile and execute on mlwsvm\n"
        "  ail bundle <file.ail> -o <bin> [--env FILE]\n"
        "                                           Bundle into a standalone executable\n"
        "  ail version                              Print version\n",
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

static const char *find_runtime_dir(char *buf, size_t cap) {
    const char *env = getenv("AIL_RUNTIME");
    if (env && env[0]) return env;
    struct stat st;
    if (stat("runtime/ail/core.masm", &st) == 0) return "runtime";
    /* Also check relative to argv0 or current dir */
    (void)buf; (void)cap;
    return "runtime";
}

static bool compile_file(const char *in_path, bool emit_code, StrBuf *out_masm, SemaInfo *out_sema) {
    size_t len = 0;
    char *src = read_entire_file(in_path, &len);
    if (!src) {
        fprintf(stderr, "ail: cannot read '%s'\n", in_path);
        return false;
    }

    Arena arena;
    arena_init(&arena);
    DiagList diags;
    diag_init(&diags);

    TokenList toks;
    lex_source(in_path, src, len, &arena, &diags, &toks);

    Node *prog = NULL;
    if (diags.error_count == 0) {
        prog = parse_program(&toks, &arena, &diags);
    }

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
        /* Copy only counts (pointers live in arena) */
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

    token_list_free(&toks);
    diag_free(&diags);
    arena_free(&arena);
    free(src);
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
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return 1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
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

    if (strcmp(cmd, "run") == 0) {
        if (argc < 3) { usage(); return 1; }
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

        char rt_buf[256];
        const char *rt = find_runtime_dir(rt_buf, sizeof(rt_buf));
        const char *vm = find_mlwsvm();

        char *vargv[128];
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

        char rt_buf[256];
        const char *rt = find_runtime_dir(rt_buf, sizeof(rt_buf));
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
