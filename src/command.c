#include "ghost_shell.h"
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <ctype.h>
#include <sys/wait.h>

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
    
    /* Check if command exists and is executable */
    char *cmd_path = NULL;
    if (cmd->name[0] == '/' || cmd->name[0] == '.') {
        /* Absolute path or relative to current directory */
        if (access(cmd->name, F_OK) != 0) {
            fprintf(stderr, "ghost-shell: no such file or directory: %s\n", cmd->name);
            return 127;
        }
        if (access(cmd->name, X_OK) != 0) {
            fprintf(stderr, "ghost-shell: permission denied: %s\n", cmd->name);
            return 126;
        }
        cmd_path = strdup(cmd->name);
    } else {
        /* Search in PATH */
        const char *path = getenv("PATH");
        if (!path) path = "/usr/local/bin:/usr/bin:/bin";
        
        char *path_copy = strdup(path);
        char *dir = strtok(path_copy, ":");
        
        while (dir) {
            char full_path[PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir, cmd->name);
            
            if (access(full_path, F_OK | X_OK) == 0) {
                cmd_path = strdup(full_path);
                break;
            }
            dir = strtok(NULL, ":");
        }
        free(path_copy);
        
        if (!cmd_path) {
            fprintf(stderr, "ghost-shell: command not found: %s\n", cmd->name);
            return 127;
        }
    }
    
    /* Fork and execute external command */
    pid = fork();
    if (pid == 0) {
        /* Child process */
        
        /* Handle input redirection */
        if (cmd->input_file) {
            if (access(cmd->input_file, F_OK) != 0) {
                fprintf(stderr, "ghost-shell: no such file or directory: %s\n", cmd->input_file);
                exit(1);
            }
            if (access(cmd->input_file, R_OK) != 0) {
                fprintf(stderr, "ghost-shell: permission denied: %s\n", cmd->input_file);
                exit(1);
            }
            FILE *input = freopen(cmd->input_file, "r", stdin);
            if (!input) {
                fprintf(stderr, "ghost-shell: failed to open input file: %s\n", cmd->input_file);
                exit(1);
            }
        }
        
        /* Handle output redirection */
        if (cmd->output_file) {
            /* Check directory permissions if file doesn't exist */
            char *dir_copy = strdup(cmd->output_file);
            char *dir_name = dirname(dir_copy);
            if (access(dir_name, W_OK) != 0) {
                fprintf(stderr, "ghost-shell: permission denied: %s\n", cmd->output_file);
                free(dir_copy);
                exit(1);
            }
            free(dir_copy);
            
            FILE *output = freopen(cmd->output_file, "w", stdout);
            if (!output) {
                fprintf(stderr, "ghost-shell: failed to open output file: %s\n", cmd->output_file);
                exit(1);
            }
        }
        
        /* Execute the command */
        execv(cmd_path, cmd->args);
        
        /* If we get here, execv failed */
        fprintf(stderr, "ghost-shell: failed to execute: %s\n", cmd->name);
        exit(126);
    } else if (pid < 0) {
        /* Fork failed */
        fprintf(stderr, "ghost-shell: fork failed: %s\n", strerror(errno));
        free(cmd_path);
        return 1;
    }
    
    free(cmd_path);
    
    /* Parent process */
    if (!cmd->background) {
        /* Wait for child if not running in background */
        waitpid(pid, &status, 0);
        if (WIFSIGNALED(status)) {
            fprintf(stderr, "ghost-shell: %s: terminated by signal %d\n", 
                    cmd->name, WTERMSIG(status));
            return 128 + WTERMSIG(status);
        }
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
    *count = 0;
    size_t max_tokens = 10;  /* Initial capacity */
    size_t token_len = 0;
    char *token = NULL;
    int in_quotes = 0;
    int escaped = 0;
    
    /* Allocate initial token array */
    tokens = calloc(max_tokens, sizeof(char*));
    if (!tokens) {
        print_error("Memory allocation failed");
        return NULL;
    }
    
    /* Allocate buffer for current token */
    token = malloc(GHOST_MAX_INPUT_SIZE);
    if (!token) {
        free(tokens);
        print_error("Memory allocation failed");
        return NULL;
    }
    
    /* Parse input character by character */
    for (size_t i = 0; line[i] != '\0'; i++) {
        if (escaped) {
            /* Previous character was a backslash */
            if (line[i] != '\n') {  /* Ignore escaped newlines */
                token[token_len++] = line[i];
            }
            escaped = 0;
            continue;
        }
        
        if (line[i] == '\\') {
            escaped = 1;
            continue;
        }
        
        if (line[i] == '"' && !escaped) {
            in_quotes = !in_quotes;
            continue;
        }
        
        if (!in_quotes && isspace(line[i])) {
            if (token_len > 0) {
                /* End of token */
                token[token_len] = '\0';
                
                /* Resize token array if needed */
                if (*count >= max_tokens) {
                    max_tokens *= 2;
                    char **new_tokens = realloc(tokens, max_tokens * sizeof(char*));
                    if (!new_tokens) {
                        free(token);
                        for (int j = 0; j < *count; j++) {
                            free(tokens[j]);
                        }
                        free(tokens);
                        print_error("Memory allocation failed");
                        return NULL;
                    }
                    tokens = new_tokens;
                }
                
                /* Add token to array */
                tokens[*count] = strdup(token);
                if (!tokens[*count]) {
                    free(token);
                    for (int j = 0; j < *count; j++) {
                        free(tokens[j]);
                    }
                    free(tokens);
                    print_error("Memory allocation failed");
                    return NULL;
                }
                (*count)++;
                token_len = 0;
            }
        } else {
            /* Add character to current token */
            token[token_len++] = line[i];
        }
    }
    
    /* Handle last token */
    if (token_len > 0) {
        token[token_len] = '\0';
        
        /* Resize token array if needed */
        if (*count >= max_tokens) {
            max_tokens++;
            char **new_tokens = realloc(tokens, max_tokens * sizeof(char*));
            if (!new_tokens) {
                free(token);
                for (int j = 0; j < *count; j++) {
                    free(tokens[j]);
                }
                free(tokens);
                print_error("Memory allocation failed");
                return NULL;
            }
            tokens = new_tokens;
        }
        
        /* Add final token */
        tokens[*count] = strdup(token);
        if (!tokens[*count]) {
            free(token);
            for (int j = 0; j < *count; j++) {
                free(tokens[j]);
            }
            free(tokens);
            print_error("Memory allocation failed");
            return NULL;
        }
        (*count)++;
    }
    
    /* Add NULL terminator */
    if (*count >= max_tokens) {
        max_tokens++;
        char **new_tokens = realloc(tokens, max_tokens * sizeof(char*));
        if (!new_tokens) {
            free(token);
            for (int j = 0; j < *count; j++) {
                free(tokens[j]);
            }
            free(tokens);
            print_error("Memory allocation failed");
            return NULL;
        }
        tokens = new_tokens;
    }
    tokens[*count] = NULL;
    
    free(token);
    return tokens;
}

static int is_builtin(const char *cmd) {
    return (strcmp(cmd, "cd") == 0 ||
            strcmp(cmd, "exit") == 0 ||
            strcmp(cmd, "help") == 0 ||
            strcmp(cmd, "history") == 0 ||
            strcmp(cmd, "call") == 0 ||
            strcmp(cmd, "export") == 0);
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
    } else if (strcmp(cmd->name, "call") == 0) {
        return builtin_call(cmd, ctx);
    } else if (strcmp(cmd->name, "export") == 0) {
        return builtin_export(cmd, ctx);
    }
    return 1;
}
