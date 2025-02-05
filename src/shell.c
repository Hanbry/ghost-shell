#include "ghost_shell.h"

/* Global variables for completion */
static char **commands = NULL;
static int num_commands = 0;

/* Global history state */
History *hist = NULL;
HistEvent ev;

/* Test if terminal supports colors */
static int test_colors(void) {
    const char *term = getenv("TERM");
    const char *colorterm = getenv("COLORTERM");
    
    if (term && (strstr(term, "color") || strstr(term, "xterm") || strstr(term, "256color"))) {
        return 1;
    }
    if (colorterm) {
        return 1;
    }
    return 0;
}

void shell_init(ShellContext *ctx) {
    const char *home = getenv("HOME");
    ctx->current_dir = getcwd(NULL, 0);
    ctx->exit_flag = 0;
    ctx->last_status = 0;
    
    if (!ctx->current_dir) {
        print_error("Failed to get current working directory");
        exit(1);
    }
    
    /* Set up history */
    hist = history_init();
    if (hist) {
        /* Configure history */
        history(hist, &ev, H_SETSIZE, GHOST_HISTORY_SIZE);
        history(hist, &ev, H_SETUNIQUE, 1);  /* Don't add duplicate entries */
        
        /* Set up history file path */
        if (home) {
            size_t len = strlen(home) + strlen("/.ghost_history") + 1;
            ctx->history_file = malloc(len);
            if (ctx->history_file) {
                snprintf(ctx->history_file, len, "%s/.ghost_history", home);
                /* Load existing history */
                history(hist, &ev, H_LOAD, ctx->history_file);
            }
        }
    }

    /* Initialize readline with our custom completion */
    initialize_readline();
}

void shell_loop(ShellContext *ctx) {
    char *input;
    Command *cmd;
    char prompt[4096];
    const char *home = getenv("HOME");
    size_t home_len = home ? strlen(home) : 0;
    int colors_supported = test_colors();
    
    /* Get username */
    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    const char *username = pw ? pw->pw_name : "user";
    
    while (!ctx->exit_flag) {
        /* Format prompt with username and current directory */
        char dir_part[4096];
        
        if (home && home_len > 0 && strncmp(ctx->current_dir, home, home_len) == 0) {
            /* Path is under HOME directory, show with tilde */
            if (ctx->current_dir[home_len] == '\0') {
                /* We're exactly at HOME */
                strncpy(dir_part, "~", sizeof(dir_part) - 1);
            } else if (ctx->current_dir[home_len] == '/') {
                /* We're in a subdirectory of HOME */
                snprintf(dir_part, sizeof(dir_part), "~%s", ctx->current_dir + home_len);
            } else {
                /* Path contains HOME as a prefix but isn't under it */
                strncpy(dir_part, ctx->current_dir, sizeof(dir_part) - 1);
            }
        } else {
            /* Path is not under HOME, show full path */
            strncpy(dir_part, ctx->current_dir, sizeof(dir_part) - 1);
        }

        if (colors_supported) {
            /* Format the prompt with colors */
            snprintf(prompt, sizeof(prompt),
                    "%s%s%s@ghsh:%s%s%s$ ",
                    COLOR_CYAN, username, COLOR_RESET,
                    COLOR_YELLOW, dir_part, COLOR_RESET);
        } else {
            /* Format the prompt without colors */
            snprintf(prompt, sizeof(prompt),
                    "%s@ghsh:%s$ ",
                    username, dir_part);
        }
        
        /* Read input using readline */
        input = readline(prompt);
        
        if (!input) {
            /* Handle EOF (Ctrl+D) */
            printf("\n");
            break;
        }
        
        /* Add input to history if not empty */
        if (input[0] != '\0') {
            add_history(input);
            if (hist && ctx->history_file) {
                history(hist, &ev, H_ENTER, input);
                history(hist, &ev, H_SAVE, ctx->history_file);
            }
            
            /* Parse and execute command */
            cmd = parse_command(input);
            if (cmd) {
                ctx->last_status = execute_command(cmd, ctx);
                free_command(cmd);
            }
        }
        
        free(input);
    }
}

void shell_cleanup(ShellContext *ctx) {
    if (ctx->current_dir) {
        free(ctx->current_dir);
        ctx->current_dir = NULL;
    }
    
    if (ctx->history_file) {
        if (hist) {
            history(hist, &ev, H_SAVE, ctx->history_file);
        }
        free(ctx->history_file);
        ctx->history_file = NULL;
    }
    
    /* Cleanup history */
    if (hist) {
        history_end(hist);
        hist = NULL;
    }
    
    /* Free command list used for completion */
    if (commands) {
        for (int i = 0; i < num_commands; i++) {
            free(commands[i]);
        }
        free(commands);
    }
}

void load_history(const char *filename) {
    read_history(filename);
    stifle_history(GHOST_HISTORY_SIZE);
}

void save_history(const char *filename) {
    write_history(filename);
}

/* Initialize readline with our custom completion */
void initialize_readline(void) {
    /* Tell readline to use our completion function */
    rl_attempted_completion_function = ghost_completion;
    
    /* Don't use default filename completion */
    rl_completion_entry_function = NULL;
    
    /* Initialize command list for completion */
    char *path = getenv("PATH");
    if (path) {
        char *path_copy = strdup(path);
        char *dir = strtok(path_copy, ":");
        
        while (dir) {
            DIR *d = opendir(dir);
            if (d) {
                struct dirent *entry;
                while ((entry = readdir(d)) != NULL) {
                    if (entry->d_type == DT_REG || entry->d_type == DT_LNK) {
                        /* Reallocate command array */
                        char **new_commands = realloc(commands, (num_commands + 1) * sizeof(char*));
                        if (new_commands) {
                            commands = new_commands;
                            commands[num_commands] = strdup(entry->d_name);
                            num_commands++;
                        }
                    }
                }
                closedir(d);
            }
            dir = strtok(NULL, ":");
        }
        free(path_copy);
    }
    
    /* Add built-in commands */
    const char *builtins[] = {"cd", "exit", "help"};
    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
        char **new_commands = realloc(commands, (num_commands + 1) * sizeof(char*));
        if (new_commands) {
            commands = new_commands;
            commands[num_commands] = strdup(builtins[i]);
            num_commands++;
        }
    }
}

/* Custom completion generator */
char *command_generator(const char *text, int state) {
    static int list_index;
    static size_t len;
    char *name;
    
    /* If this is a new word to complete, initialize */
    if (!state) {
        list_index = 0;
        len = strlen(text);
    }
    
    /* Return the next name which partially matches */
    while (list_index < num_commands) {
        name = commands[list_index];
        list_index++;
        
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }
    
    /* If no names matched, return NULL */
    return NULL;
}

/* Custom completion function */
char **ghost_completion(const char *text, int start, int end) {
    char **matches = NULL;
    
    (void)end;  /* Unused parameter */
    
    /* If this word is at the start of the line, match commands */
    if (start == 0) {
        matches = rl_completion_matches(text, command_generator);
    } else {
        /* Default to filename completion */
        matches = rl_completion_matches(text, rl_filename_completion_function);
    }
    
    return matches;
}

char *read_line(void) {
    /* This function is now unused as we use readline directly */
    return NULL;
}

void print_error(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
} 