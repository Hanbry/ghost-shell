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

int builtin_cd(ghost_command *cmd, shell_context *ctx) {
    const char *dir = cmd->arg_count > 1 ? cmd->args[1] : getenv("HOME");
    if (!dir) {
        print_error("HOME environment variable not set");
        return 1;
    }
    
    if (chdir(dir) != 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "cd: %s: %s", dir, strerror(errno));
        print_error(error_msg);
        return 1;
    }
    
    /* Update current directory */
    char *new_dir = getcwd(NULL, 0);
    if (new_dir) {
        free(ctx->current_dir);
        ctx->current_dir = new_dir;
    }
    
    return 0;
}

int builtin_exit(ghost_command *cmd, shell_context *ctx) {
    int exit_status = 0;
    
    if (cmd->arg_count > 1) {
        char *endptr;
        exit_status = strtol(cmd->args[1], &endptr, 10);
        if (*endptr != '\0') {
            print_error("exit: numeric argument required");
            exit_status = 2;
        }
    }
    
    ctx->exit_flag = 1;
    return exit_status;
}

int builtin_help(ghost_command *cmd, shell_context *ctx) {
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

int builtin_history(ghost_command *cmd, shell_context *ctx) {
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

int builtin_call(ghost_command *cmd, shell_context *ctx) {
    if (cmd->arg_count < 2) {
        print_error("call: missing prompt argument");
        return 1;
    }
    
    /* Initialize AI context if needed */
    if (!ctx->ai_ctx) {
        ctx->ai_ctx = ghost_ai_init();
        if (!ctx->ai_ctx) {
            print_error("Failed to initialize AI context");
            return 1;
        }
    }
    
    /* Combine all arguments into a single prompt */
    size_t total_len = 0;
    for (int i = 1; i < cmd->arg_count; i++) {
        total_len += strlen(cmd->args[i]) + 1;  /* +1 for space */
    }
    
    char *prompt = malloc(total_len + 1);  /* +1 for null terminator */
    if (!prompt) {
        print_error("Memory allocation failed");
        return 1;
    }
    
    prompt[0] = '\0';
    for (int i = 1; i < cmd->arg_count; i++) {
        if (i > 1) strcat(prompt, " ");
        strcat(prompt, cmd->args[i]);
    }
    
    /* Store prompt for analysis */
    free(ctx->last_prompt);
    ctx->last_prompt = strdup(prompt);
    
    /* Process prompt */
    ctx->ai_ctx->is_ghost_mode = 1;
    int result = ghost_ai_process(prompt, ctx->ai_ctx, ctx);
    ctx->ai_ctx->is_ghost_mode = 0;
    
    free(prompt);
    return result;
}

int builtin_export(ghost_command *cmd, shell_context *ctx) {
    (void)ctx;  /* Unused parameter */
    
    if (cmd->arg_count == 1) {
        /* List all environment variables */
        extern char **environ;
        for (char **env = environ; *env != NULL; env++) {
            printf("%s\n", *env);
        }
        return 0;
    }
    
    /* Process each NAME=VALUE argument */
    for (int i = 1; i < cmd->arg_count; i++) {
        char *arg = strdup(cmd->args[i]);
        char *equals = strchr(arg, '=');
        
        if (!equals) {
            /* Just a NAME - print its value */
            const char *value = getenv(arg);
            if (value) {
                printf("%s=%s\n", arg, value);
            }
        } else {
            /* NAME=VALUE - set it */
            *equals = '\0';  /* Split into name and value */
            if (setenv(arg, equals + 1, 1) != 0) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), 
                        "export: %s: %s", arg, strerror(errno));
                print_error(error_msg);
                free(arg);
                return 1;
            }
        }
        free(arg);
    }
    
    return 0;
}
