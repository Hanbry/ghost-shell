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
#include <fcntl.h>
#include <histedit.h>

/* Forward declarations of static functions */
static ghost_command *parse_single_command(char *input, char **next_cmd);
static int is_builtin(const char *cmd);
static int handle_builtin(ghost_command *cmd, shell_context *ctx);

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

/* Helper function to read here-document content */
static char *read_here_doc(const char *delimiter) {
    char *content = malloc(GHOST_MAX_INPUT_SIZE);
    char *line = NULL;
    size_t total_len = 0;
    EditLine *edit_line = el_init("ghost-shell", stdin, stdout, stderr);
    
    if (!content || !edit_line) {
        free(content);
        if (edit_line) el_end(edit_line);
        return NULL;
    }
    content[0] = '\0';
    
    printf("heredoc> ");
    int count;
    while ((line = (char *)el_gets(edit_line, &count)) != NULL) {
        /* Remove trailing newline */
        if (count > 0) {
            line[count-1] = '\0';
        }
        
        if (strcmp(line, delimiter) == 0) {
            break;
        }
        
        size_t line_len = strlen(line);
        if (total_len + line_len + 2 >= GHOST_MAX_INPUT_SIZE) {
            fprintf(stderr, "ghost-shell: here-document too large\n");
            el_end(edit_line);
            free(content);
            return NULL;
        }
        
        strcat(content, line);
        strcat(content, "\n");
        total_len += line_len + 1;
        printf("heredoc> ");
    }
    
    el_end(edit_line);
    return content;
}

ghost_command *parse_command(const char *input) {
    char *input_copy = strdup(input);
    char *next_cmd = NULL;
    ghost_command *first_cmd = NULL;
    ghost_command *current_cmd = NULL;
    char *cmd_str = input_copy;
    
    while (cmd_str && *cmd_str) {
        ghost_command *cmd = parse_single_command(cmd_str, &next_cmd);
        if (!cmd) {
            if (first_cmd) free_command(first_cmd);
            free(input_copy);
            return NULL;
        }
        
        if (!first_cmd) {
            first_cmd = cmd;
            current_cmd = cmd;
        } else {
            current_cmd->next = cmd;
            current_cmd = cmd;
        }
        
        cmd_str = next_cmd;
    }
    
    free(input_copy);
    return first_cmd;
}

static ghost_command *parse_single_command(char *input, char **next_cmd) {
    ghost_command *cmd = calloc(1, sizeof(ghost_command));
    char *expanded_input;
    char *pipe_pos;
    
    if (!cmd) return NULL;
    
    /* Find pipe if it exists */
    pipe_pos = strchr(input, '|');
    if (pipe_pos) {
        *pipe_pos = '\0';
        *next_cmd = pipe_pos + 1;
        while (**next_cmd == ' ') (*next_cmd)++;  /* Skip spaces after pipe */
    } else {
        *next_cmd = NULL;
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
    for (size_t i = 0; i < cmd->arg_count; i++) {
        if (strcmp(cmd->args[i], "<<") == 0 && i + 1 < cmd->arg_count) {
            /* Here document */
            cmd->here_doc = read_here_doc(cmd->args[i + 1]);
            if (!cmd->here_doc) {
                free_command(cmd);
                return NULL;
            }
            /* Remove redirection from args */
            memmove(&cmd->args[i], &cmd->args[i + 2],
                    (cmd->arg_count - i - 2) * sizeof(char*));
            cmd->arg_count -= 2;
            i--;
        } else if (strcmp(cmd->args[i], "<") == 0 && i + 1 < cmd->arg_count) {
            /* Input redirection */
            cmd->input_file = strdup(cmd->args[i + 1]);
            memmove(&cmd->args[i], &cmd->args[i + 2],
                    (cmd->arg_count - i - 2) * sizeof(char*));
            cmd->arg_count -= 2;
            i--;
        } else if (strcmp(cmd->args[i], ">>") == 0 && i + 1 < cmd->arg_count) {
            /* Append output */
            cmd->output_file = strdup(cmd->args[i + 1]);
            cmd->append_output = 1;
            memmove(&cmd->args[i], &cmd->args[i + 2],
                    (cmd->arg_count - i - 2) * sizeof(char*));
            cmd->arg_count -= 2;
            i--;
        } else if (strcmp(cmd->args[i], ">") == 0 && i + 1 < cmd->arg_count) {
            /* Output redirection */
            cmd->output_file = strdup(cmd->args[i + 1]);
            cmd->append_output = 0;
            memmove(&cmd->args[i], &cmd->args[i + 2],
                    (cmd->arg_count - i - 2) * sizeof(char*));
            cmd->arg_count -= 2;
            i--;
        } else if (strcmp(cmd->args[i], "&") == 0) {
            cmd->background = 1;
            free(cmd->args[i]);
            cmd->arg_count--;
        }
    }
    
    /* Null terminate the argument list */
    cmd->args[cmd->arg_count] = NULL;
    
    return cmd;
}

int execute_command(ghost_command *cmd, shell_context *ctx) {
    if (!cmd) return 1;
    
    /* Handle built-in commands (only for non-piped commands) */
    if (!cmd->next && is_builtin(cmd->name)) {
        return handle_builtin(cmd, ctx);
    }
    
    int status = 0;
    int prev_pipe[2] = {STDIN_FILENO, STDOUT_FILENO};
    ghost_command *current = cmd;
    pid_t *pids = NULL;  /* Array to store all process IDs */
    int pid_count = 0;
    
    /* Count number of commands in pipeline */
    for (ghost_command *c = cmd; c != NULL; c = c->next) {
        pid_count++;
    }
    
    /* Allocate pid array */
    pids = malloc(sizeof(pid_t) * pid_count);
    if (!pids) {
        perror("ghost-shell: malloc failed");
        return 1;
    }
    
    int cmd_index = 0;
    while (current) {
        int pipe_fds[2] = {STDIN_FILENO, STDOUT_FILENO};
        
        /* Create pipe if there's a next command */
        if (current->next) {
            if (pipe(pipe_fds) < 0) {
                perror("ghost-shell: pipe failed");
                free(pids);
                return 1;
            }
        }
        
        pid_t pid = fork();
        if (pid == 0) {
            /* Child process */
            free(pids);  /* Child doesn't need this */
            
            /* Set up input from previous pipe */
            if (prev_pipe[0] != STDIN_FILENO) {
                dup2(prev_pipe[0], STDIN_FILENO);
                close(prev_pipe[0]);
            }
            if (prev_pipe[1] != STDOUT_FILENO)
                close(prev_pipe[1]);
            
            /* Set up output to next pipe */
            if (current->next) {
                close(pipe_fds[0]);  /* Close read end */
                dup2(pipe_fds[1], STDOUT_FILENO);
                close(pipe_fds[1]);
            }
            
            /* Handle input redirection or here-document */
            if (current->here_doc) {
                /* Create temporary pipe for here-document */
                int here_pipe[2];
                if (pipe(here_pipe) == 0) {
                    write(here_pipe[1], current->here_doc, strlen(current->here_doc));
                    close(here_pipe[1]);
                    dup2(here_pipe[0], STDIN_FILENO);
                    close(here_pipe[0]);
                }
            } else if (current->input_file) {
                int fd = open(current->input_file, O_RDONLY);
                if (fd < 0) {
                    fprintf(stderr, "ghost-shell: cannot open %s: %s\n",
                            current->input_file, strerror(errno));
                    exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            
            /* Handle output redirection */
            if (current->output_file) {
                int flags = O_WRONLY | O_CREAT;
                flags |= current->append_output ? O_APPEND : O_TRUNC;
                
                int fd = open(current->output_file, flags, 0644);
                if (fd < 0) {
                    fprintf(stderr, "ghost-shell: cannot open %s: %s\n",
                            current->output_file, strerror(errno));
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
            
            /* Execute the command */
            if (execvp(current->name, current->args) < 0) {
                fprintf(stderr, "ghost-shell: %s: command not found\n", current->name);
                exit(127);
            }
            exit(1);  /* Should never reach here */
        } else if (pid < 0) {
            perror("ghost-shell: fork failed");
            free(pids);
            return 1;
        }
        
        /* Parent process */
        pids[cmd_index++] = pid;
        
        /* Close previous pipe fds */
        if (prev_pipe[0] != STDIN_FILENO)
            close(prev_pipe[0]);
        if (prev_pipe[1] != STDOUT_FILENO)
            close(prev_pipe[1]);
        
        /* Set up for next command */
        if (current->next) {
            prev_pipe[0] = pipe_fds[0];
            prev_pipe[1] = pipe_fds[1];
        }
        
        current = current->next;
    }
    
    /* Parent waits for all processes unless in background */
    if (!cmd->background) {
        for (int i = 0; i < pid_count; i++) {
            waitpid(pids[i], &status, 0);
        }
        
        /* Return the status of the last command in the pipeline */
        if (WIFSIGNALED(status)) {
            fprintf(stderr, "ghost-shell: terminated by signal %d\n", WTERMSIG(status));
            free(pids);
            return 128 + WTERMSIG(status);
        }
    }
    
    free(pids);
    return WEXITSTATUS(status);
}

void free_command(ghost_command *cmd) {
    if (!cmd) return;
    
    if (cmd->name) free(cmd->name);
    if (cmd->args) {
        for (size_t i = 0; i < cmd->arg_count; i++) {
            if (cmd->args[i]) free(cmd->args[i]);
        }
        free(cmd->args);
    }
    if (cmd->input_file) free(cmd->input_file);
    if (cmd->output_file) free(cmd->output_file);
    if (cmd->here_doc) free(cmd->here_doc);
    if (cmd->next) free_command(cmd->next);
    free(cmd);
}

/* Helper function to split a line into tokens */
char **split_line(const char *line, size_t *count) {
    if (!line || !count) return NULL;
    *count = 0;
    
    /* Initial allocation */
    size_t max_tokens = 10;
    char **tokens = malloc(max_tokens * sizeof(char*));
    if (!tokens) {
        print_error("Memory allocation failed");
        return NULL;
    }
    
    /* Allocate buffer for current token */
    char *token = malloc(GHOST_MAX_INPUT_SIZE);
    if (!token) {
        free(tokens);
        print_error("Memory allocation failed");
        return NULL;
    }
    
    size_t token_len = 0;
    int in_quotes = 0;
    int escaped = 0;
    
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
                        for (size_t j = 0; j < *count; j++) {
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
                    for (size_t j = 0; j < *count; j++) {
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
            max_tokens *= 2;
            char **new_tokens = realloc(tokens, max_tokens * sizeof(char*));
            if (!new_tokens) {
                free(token);
                for (size_t j = 0; j < *count; j++) {
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
            for (size_t j = 0; j < *count; j++) {
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
            for (size_t j = 0; j < *count; j++) {
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
            strcmp(cmd, "export") == 0 ||
            strcmp(cmd, ".") == 0 ||
            strcmp(cmd, "source") == 0);
}

static int handle_builtin(ghost_command *cmd, shell_context *ctx) {
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
    } else if (strcmp(cmd->name, ".") == 0 || strcmp(cmd->name, "source") == 0) {
        return builtin_source(cmd, ctx);
    }
    return 1;
}
