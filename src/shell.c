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
static char *get_prompt(EditLine *edit_line);

/* Prompt function for libedit */
static char *get_prompt(EditLine *edit_line) {
    (void)edit_line;
    static char prompt[1024];
    const char *username = getenv("USER");
    if (!username) username = "user";

    char *path = get_formatted_path();
    format_shell_prompt(prompt, sizeof(prompt), username, path);
    free(path);
    return strdup(prompt);
}

void shell_init(shell_context *ctx) {
    ctx->current_dir = getcwd(NULL, 0);
    ctx->exit_flag = 0;
    ctx->last_status = 0;
    ctx->ai_ctx = NULL;
    ctx->last_prompt = NULL;

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
        el_set(el, EL_ADDFN, "complete", "Complete command", ghost_complete);
        el_set(el, EL_BIND, "^I", "complete", NULL);
        
        /* Load other default bindings */
        el_source(el, NULL);
    }
}

void shell_loop(shell_context *ctx) {
    const char *line;
    int count;
    ghost_command *cmd;

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

void shell_cleanup(shell_context *ctx) {
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

    if (ctx->last_prompt) {
        free(ctx->last_prompt);
        ctx->last_prompt = NULL;
    }

    /* Clean up completion system */
    completions_cleanup();
}

void print_error(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
}
