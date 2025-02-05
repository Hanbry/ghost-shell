#include "ghost_shell.h"

/* Declare external history state */
extern History *hist;
extern HistEvent ev;

int builtin_cd(Command *cmd, ShellContext *ctx) {
    char *new_dir;
    
    /* If no argument is provided, change to HOME directory */
    if (cmd->arg_count == 1) {
        new_dir = getenv("HOME");
        if (!new_dir) {
            print_error("HOME environment variable not set");
            return 1;
        }
    } else {
        new_dir = cmd->args[1];
    }
    
    /* Change directory */
    if (chdir(new_dir) != 0) {
        print_error("Failed to change directory");
        return 1;
    }
    
    /* Update current directory in context */
    free(ctx->current_dir);
    ctx->current_dir = getcwd(NULL, 0);
    if (!ctx->current_dir) {
        print_error("Failed to get current working directory");
        return 1;
    }
    
    return 0;
}

int builtin_exit(Command *cmd, ShellContext *ctx) {
    int exit_status = 0;
    
    /* If an argument is provided, use it as exit status */
    if (cmd->arg_count > 1) {
        exit_status = atoi(cmd->args[1]);
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
    printf("history      Display command history\n\n");
    printf("Features:\n");
    printf("- Input/output redirection using < and >\n");
    printf("- Background execution using &\n");
    printf("- Command history (use arrow keys)\n");
    printf("- Tab completion for commands and files\n\n");
    
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