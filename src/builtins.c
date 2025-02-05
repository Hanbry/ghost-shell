#include "ghost_shell.h"
#include "ghost_ai.h"
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>

/* Declare external history state */
extern History *hist;
extern HistEvent ev;

int builtin_cd(Command *cmd, ShellContext *ctx) {
    char *new_dir;
    
    /* If no argument is provided, change to HOME directory */
    if (cmd->arg_count == 1) {
        new_dir = getenv("HOME");
        if (!new_dir) {
            fprintf(stderr, "ghost-shell: cd: HOME not set\n");
            return 1;
        }
    } else {
        new_dir = cmd->args[1];
    }
    
    /* Check if directory exists */
    if (access(new_dir, F_OK) != 0) {
        fprintf(stderr, "ghost-shell: cd: no such file or directory: %s\n", new_dir);
        return 1;
    }
    
    /* Check if it's a directory */
    struct stat st;
    if (stat(new_dir, &st) == 0 && !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "ghost-shell: cd: not a directory: %s\n", new_dir);
        return 1;
    }
    
    /* Check if we have permission */
    if (access(new_dir, X_OK) != 0) {
        fprintf(stderr, "ghost-shell: cd: permission denied: %s\n", new_dir);
        return 1;
    }
    
    /* Change directory */
    if (chdir(new_dir) != 0) {
        fprintf(stderr, "ghost-shell: cd: %s: %s\n", new_dir, strerror(errno));
        return 1;
    }
    
    /* Update current directory in context */
    free(ctx->current_dir);
    ctx->current_dir = getcwd(NULL, 0);
    if (!ctx->current_dir) {
        fprintf(stderr, "ghost-shell: cd: failed to get current directory: %s\n", strerror(errno));
        return 1;
    }
    
    return 0;
}

int builtin_exit(Command *cmd, ShellContext *ctx) {
    int exit_status = 0;
    
    /* If an argument is provided, use it as exit status */
    if (cmd->arg_count > 1) {
        char *endptr;
        exit_status = strtol(cmd->args[1], &endptr, 10);
        if (*endptr != '\0') {
            fprintf(stderr, "ghost-shell: exit: numeric argument required\n");
            exit_status = 255;
        }
    }
    
    ctx->exit_flag = 1;
    ctx->last_status = exit_status;
    return exit_status;
}

int builtin_help(Command *cmd, ShellContext *ctx) {
    (void)cmd;  /* Unused parameter */
    (void)ctx;  /* Unused parameter */
    
    printf("\nGhost Shell v%s - Built-in commands:\n\n", GHOST_SHELL_VERSION);
    printf("cd [dir]     Change the current directory (default: HOME)\n");
    printf("exit [n]     Exit the shell with status n (default: 0)\n");
    printf("help         Display this help message\n");
    printf("history      Display command history\n");
    printf("call <prompt> Process a prompt using AI\n");
    printf("export [NAME=VALUE]  Set environment variable (no args: list all)\n\n");
    printf("Features:\n");
    printf("- Input/output redirection using < and >\n");
    printf("- Background execution using &\n");
    printf("- Command history (use arrow keys)\n");
    printf("- Tab completion for commands and files\n");
    printf("- AI assistance with the 'call' command\n\n");
    
    return 0;
}

int builtin_history(Command *cmd, ShellContext *ctx) {
    (void)cmd;  /* Unused parameter */
    (void)ctx;  /* Unused parameter */
    
#ifdef USE_GNU_READLINE
    /* GNU readline implementation */
    HIST_ENTRY **history = history_list();
    if (history) {
        for (int i = 0; history[i]; i++) {
            printf("%5d  %s\n", i + 1, history[i]->line);
        }
    }
#else
    /* libedit implementation */
    if (hist) {
        /* Move to the end of history */
        history(hist, &ev, H_LAST);
        
        /* Print history entries from newest to oldest */
        int i = 1;
        do {
            printf("%5d  %s\n", i++, ev.str);
        } while (history(hist, &ev, H_PREV) == 0);
        
        /* Reset the history position */
        history(hist, &ev, H_LAST);
    }
#endif
    
    return 0;
}

int builtin_call(Command *cmd, ShellContext *ctx) {
    if (!cmd || !ctx) {
        fprintf(stderr, "Usage: call <prompt>\n");
        return 1;
    }

    if (cmd->arg_count < 2) {
        fprintf(stderr, "Usage: call <prompt>\n");
        return 1;
    }

    /* Initialize AI context if not already done */
    if (!ctx->ai_ctx) {
        ctx->ai_ctx = ghost_ai_init();
        if (!ctx->ai_ctx) {
            fprintf(stderr, "Failed to initialize AI context\n");
            return 1;
        }
    }

    /* Combine all arguments into a single prompt */
    char prompt[4096] = {0};
    for (int i = 1; i < cmd->arg_count; i++) {
        if (!cmd->args[i]) {
            continue;
        }
        if (i > 1) strcat(prompt, " ");
        strcat(prompt, cmd->args[i]);
    }
    
    /* Process the prompt */
    ctx->ai_ctx->in_ghost_mode = 1;
    int result = ghost_ai_process(prompt, ctx->ai_ctx, ctx);
    ctx->ai_ctx->in_ghost_mode = 0;
    
    return result;
}

int builtin_export(Command *cmd, ShellContext *ctx) {
    (void)ctx;  /* Unused parameter */
    
    if (cmd->arg_count < 2) {
        /* If no arguments, list all environment variables */
        extern char **environ;
        for (char **env = environ; *env != NULL; env++) {
            printf("%s\n", *env);
        }
        return 0;
    }
    
    /* Process each argument */
    for (int i = 1; i < cmd->arg_count; i++) {
        char *arg = cmd->args[i];
        char *equals = strchr(arg, '=');
        
        if (!equals) {
            fprintf(stderr, "ghost-shell: export: invalid format: %s\n", arg);
            fprintf(stderr, "Usage: export NAME=VALUE\n");
            return 1;
        }
        
        /* Temporarily split string at '=' */
        *equals = '\0';
        const char *name = arg;
        const char *value = equals + 1;
        
        /* Set environment variable */
        if (setenv(name, value, 1) != 0) {
            fprintf(stderr, "ghost-shell: export: failed to set %s: %s\n", 
                    name, strerror(errno));
            *equals = '=';  /* Restore string */
            return 1;
        }
        
        *equals = '=';  /* Restore string */
    }
    
    return 0;
}
