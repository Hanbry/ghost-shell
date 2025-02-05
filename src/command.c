#include "ghost_shell.h"

/* Forward declarations of static functions */
static int is_builtin(const char *cmd);
static int handle_builtin(Command *cmd, ShellContext *ctx);

/* Helper function to expand environment variables in a string */
static char *expand_env_vars(const char *str) {
    char *result = malloc(GHOST_MAX_INPUT_SIZE);
    size_t i = 0, j = 0;
    
    if (!result) {
        print_error("Memory allocation failed");
        return NULL;
    }
    
    while (str[i] && j < GHOST_MAX_INPUT_SIZE - 1) {
        if (str[i] == '$' && str[i + 1]) {
            /* Found a potential environment variable */
            char var_name[256] = {0};
            size_t var_len = 0;
            i++; /* Skip $ */
            
            /* Extract variable name */
            if (str[i] == '{') {
                i++; /* Skip { */
                while (str[i] && str[i] != '}' && var_len < 255) {
                    var_name[var_len++] = str[i++];
                }
                if (str[i] == '}') i++; /* Skip } */
            } else {
                while ((isalnum(str[i]) || str[i] == '_') && var_len < 255) {
                    var_name[var_len++] = str[i++];
                }
            }
            
            /* Get environment variable value */
            const char *value = getenv(var_name);
            if (value) {
                size_t val_len = strlen(value);
                if (j + val_len < GHOST_MAX_INPUT_SIZE - 1) {
                    strcpy(result + j, value);
                    j += val_len;
                }
            }
        } else {
            result[j++] = str[i++];
        }
    }
    
    result[j] = '\0';
    return result;
}

Command *parse_command(const char *input) {
    Command *cmd = calloc(1, sizeof(Command));
    char *expanded_input;
    
    if (!cmd) {
        print_error("Memory allocation failed");
        return NULL;
    }
    
    /* Expand environment variables in input */
    expanded_input = expand_env_vars(input);
    if (!expanded_input) {
        free(cmd);
        return NULL;
    }
    
    /* Split input into tokens */
    cmd->args = split_line(expanded_input, &cmd->arg_count);
    free(expanded_input);
    
    if (!cmd->args || cmd->arg_count == 0) {
        free(cmd);
        return NULL;
    }
    
    /* First argument is the command name */
    cmd->name = strdup(cmd->args[0]);
    if (!cmd->name) {
        print_error("Memory allocation failed");
        free_command(cmd);
        return NULL;
    }
    
    /* Parse for redirections and background execution */
    for (int i = 0; i < cmd->arg_count; i++) {
        if (strcmp(cmd->args[i], "<") == 0 && i + 1 < cmd->arg_count) {
            cmd->input_file = strdup(cmd->args[i + 1]);
            /* Remove redirection from args */
            memmove(&cmd->args[i], &cmd->args[i + 2], 
                    (cmd->arg_count - i - 2) * sizeof(char*));
            cmd->arg_count -= 2;
            i--;
        } else if (strcmp(cmd->args[i], ">") == 0 && i + 1 < cmd->arg_count) {
            cmd->output_file = strdup(cmd->args[i + 1]);
            /* Remove redirection from args */
            memmove(&cmd->args[i], &cmd->args[i + 2],
                    (cmd->arg_count - i - 2) * sizeof(char*));
            cmd->arg_count -= 2;
            i--;
        } else if (strcmp(cmd->args[i], "&") == 0) {
            cmd->background = 1;
            /* Remove & from args */
            free(cmd->args[i]);
            cmd->arg_count--;
        }
    }
    
    /* Null terminate the argument list */
    cmd->args[cmd->arg_count] = NULL;
    
    return cmd;
}

int execute_command(Command *cmd, ShellContext *ctx) {
    pid_t pid;
    int status = 0;
    
    /* Check for built-in commands first */
    if (is_builtin(cmd->name)) {
        return handle_builtin(cmd, ctx);
    }
    
    /* Fork and execute external command */
    pid = fork();
    if (pid == 0) {
        /* Child process */
        
        /* Handle input redirection */
        if (cmd->input_file) {
            FILE *input = freopen(cmd->input_file, "r", stdin);
            if (!input) {
                print_error("Failed to open input file");
                exit(1);
            }
        }
        
        /* Handle output redirection */
        if (cmd->output_file) {
            FILE *output = freopen(cmd->output_file, "w", stdout);
            if (!output) {
                print_error("Failed to open output file");
                exit(1);
            }
        }
        
        /* Execute the command */
        execvp(cmd->name, cmd->args);
        
        /* If we get here, execvp failed */
        print_error("Command execution failed");
        exit(1);
    } else if (pid < 0) {
        /* Fork failed */
        print_error("Fork failed");
        return 1;
    }
    
    /* Parent process */
    if (!cmd->background) {
        /* Wait for child if not running in background */
        waitpid(pid, &status, 0);
        return WEXITSTATUS(status);
    }
    
    return 0;
}

void free_command(Command *cmd) {
    if (!cmd) return;
    
    if (cmd->name) free(cmd->name);
    if (cmd->input_file) free(cmd->input_file);
    if (cmd->output_file) free(cmd->output_file);
    
    if (cmd->args) {
        for (int i = 0; i < cmd->arg_count; i++) {
            free(cmd->args[i]);
        }
        free(cmd->args);
    }
    
    free(cmd);
}

char **split_line(const char *line, int *count) {
    char **tokens = NULL;
    char *token;
    char *line_copy = strdup(line);
    *count = 0;
    
    if (!line_copy) {
        print_error("Memory allocation failed");
        return NULL;
    }
    
    /* First pass: count tokens */
    token = strtok(line_copy, " \t\r\n");
    while (token) {
        (*count)++;
        token = strtok(NULL, " \t\r\n");
    }
    
    /* Allocate token array (plus one for NULL terminator) */
    tokens = calloc(*count + 1, sizeof(char*));
    if (!tokens) {
        free(line_copy);
        print_error("Memory allocation failed");
        return NULL;
    }
    
    /* Second pass: fill token array */
    strcpy(line_copy, line);
    token = strtok(line_copy, " \t\r\n");
    for (int i = 0; i < *count; i++) {
        tokens[i] = strdup(token);
        token = strtok(NULL, " \t\r\n");
    }
    
    free(line_copy);
    return tokens;
}

static int is_builtin(const char *cmd) {
    return (strcmp(cmd, "cd") == 0 ||
            strcmp(cmd, "exit") == 0 ||
            strcmp(cmd, "help") == 0 ||
            strcmp(cmd, "history") == 0);
}

static int handle_builtin(Command *cmd, ShellContext *ctx) {
    if (strcmp(cmd->name, "cd") == 0) {
        return builtin_cd(cmd, ctx);
    } else if (strcmp(cmd->name, "exit") == 0) {
        return builtin_exit(cmd, ctx);
    } else if (strcmp(cmd->name, "help") == 0) {
        return builtin_help(cmd, ctx);
    } else if (strcmp(cmd->name, "history") == 0) {
        return builtin_history(cmd, ctx);
    }
    return 1;
} 