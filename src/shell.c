#include "ghost_shell.h"
#include "completions.h"
#include <histedit.h>
#include <sys/stat.h>
#include <limits.h>
#include <libgen.h>
#include <sys/ioctl.h>

/* Global state */
static EditLine *el = NULL;
History *hist = NULL;  /* Non-static to match extern */
HistEvent ev;          /* Non-static to match extern */

/* Forward declarations */
static char *get_prompt(EditLine *e);

/* Prompt function for libedit */
static char *get_prompt(EditLine *e) {
    (void)e;
    static char prompt[1024];
    const char *username = getenv("USER");
    if (!username) username = "user";

    /* Get current directory */
    char *cwd = getcwd(NULL, 0);
    if (!cwd) {
        snprintf(prompt, sizeof(prompt), "%s@ghsh:???$ ", username);
        return strdup(prompt);
    }

    /* Try to replace home directory with ~ */
    const char *dir = cwd;
    const char *home = getenv("HOME");
    if (home && strncmp(cwd, home, strlen(home)) == 0) {
        if (cwd[strlen(home)] == '\0') {
            dir = "~";
        } else if (cwd[strlen(home)] == '/') {
            static char tilde_path[1024];
            snprintf(tilde_path, sizeof(tilde_path), "~%s", cwd + strlen(home));
            dir = tilde_path;
        }
    }

    /* Format prompt */
    snprintf(prompt, sizeof(prompt), "%s@ghsh:%s$ ", username, dir);
    free(cwd);
    return strdup(prompt);
}

void shell_init(ShellContext *ctx) {
    ctx->current_dir = getcwd(NULL, 0);
    ctx->exit_flag = 0;
    ctx->last_status = 0;
    ctx->ai_ctx = NULL;

    if (!ctx->current_dir) {
        print_error("Failed to get current working directory");
        exit(1);
    }

    /* Initialize history */
    hist = history_init();
    if (hist) {
        history(hist, &ev, H_SETSIZE, 100);
        
        /* Load history file */
        const char *home = getenv("HOME");
        if (home) {
            ctx->history_file = malloc(strlen(home) + 20);
            if (ctx->history_file) {
                sprintf(ctx->history_file, "%s/.ghost_history", home);
                history(hist, &ev, H_LOAD, ctx->history_file);
            }
        }
    }

    /* Initialize completion system */
    completions_init();

    /* Initialize EditLine */
    el = el_init("ghost-shell", stdin, stdout, stderr);
    if (el) {
        el_set(el, EL_PROMPT, get_prompt);
        el_set(el, EL_EDITOR, "emacs");
        el_set(el, EL_HIST, history, hist);
        
        /* Set up completion */
        el_set(el, EL_ADDFN, "complete", "Complete command", complete);
        el_set(el, EL_BIND, "^I", "complete", NULL);
        
        /* Load other default bindings */
        el_source(el, NULL);
    }
}

void shell_loop(ShellContext *ctx) {
    const char *line;
    int count;
    Command *cmd;

    while (!ctx->exit_flag) {
        /* Read line */
        line = el_gets(el, &count);
        if (!line || count <= 0) {
            printf("\n");
            break;
        }

        /* Skip empty lines */
        if (count <= 1) continue;

        /* Remove trailing newline */
        char *input = strdup(line);
        input[strlen(input) - 1] = '\0';

        /* Add to history */
        if (hist && input[0] != '\0') {
            history(hist, &ev, H_ENTER, input);
            if (ctx->history_file) {
                history(hist, &ev, H_SAVE, ctx->history_file);
            }
        }

        /* Parse and execute */
        cmd = parse_command(input);
        if (cmd) {
            ctx->last_status = execute_command(cmd, ctx);
            free_command(cmd);
        }

        free(input);
    }
}

void shell_cleanup(ShellContext *ctx) {
    if (el) {
        el_end(el);
        el = NULL;
    }

    if (hist) {
        if (ctx->history_file) {
            history(hist, &ev, H_SAVE, ctx->history_file);
        }
        history_end(hist);
        hist = NULL;
    }

    if (ctx->history_file) {
        free(ctx->history_file);
        ctx->history_file = NULL;
    }

    if (ctx->current_dir) {
        free(ctx->current_dir);
        ctx->current_dir = NULL;
    }

    /* Clean up completion system */
    completions_cleanup();
}

void print_error(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
}
