#include "completions.h"
#include "ghost_shell.h"
#include <sys/stat.h>
#include <limits.h>
#include <libgen.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

/* Completion state */
static char **commands = NULL;
static size_t num_commands = 0;

/* Helper function to format completions in columns */
static void print_completions_columns(char **items, size_t count) {
    if (!items || count == 0) return;
    
    /* Find the maximum width of completions */
    size_t max_width = 0;
    for (size_t i = 0; i < count; i++) {
        size_t len = strlen(items[i]);
        if (len > max_width) max_width = len;
    }
    
    /* Add padding between columns */
    max_width += 2;
    
    /* Get terminal width */
    struct winsize ws;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    int term_width = ws.ws_col;
    
    /* Calculate number of columns */
    int num_cols = term_width / max_width;
    if (num_cols == 0) num_cols = 1;
    
    /* Calculate number of rows */
    int num_rows = (count + num_cols - 1) / num_cols;
    
    printf("\n");
    /* Print completions in columns */
    for (int row = 0; row < num_rows; row++) {
        for (int col = 0; col < num_cols; col++) {
            int index = col * num_rows + row;
            if ((size_t)index < count) {
                printf("%-*s", (int)max_width, items[index]);
            }
        }
        printf("\n");
    }
}

/* Initialize command list for completion */
void completions_init(void) {
    /* Add built-in commands */
    const char *builtins[] = {"cd", "exit", "help", "history", "call", "export", "source", "."};
    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
        char **new_commands = realloc(commands, (num_commands + 1) * sizeof(char*));
        if (new_commands) {
            commands = new_commands;
            commands[num_commands++] = strdup(builtins[i]);
        }
    }

    /* Add commands from PATH */
    const char *path = getenv("PATH");
    if (path) {
        char *path_copy = strdup(path);
        char *dir = strtok(path_copy, ":");
        
        while (dir) {
            DIR *d = opendir(dir);
            if (d) {
                struct dirent *entry;
                while ((entry = readdir(d)) != NULL) {
                    if (entry->d_type == DT_REG || entry->d_type == DT_LNK) {
                        char full_path[PATH_MAX];
                        snprintf(full_path, sizeof(full_path), "%s/%s", dir, entry->d_name);
                        if (access(full_path, X_OK) == 0) {
                            char **new_commands = realloc(commands, (num_commands + 1) * sizeof(char*));
                            if (new_commands) {
                                commands = new_commands;
                                commands[num_commands++] = strdup(entry->d_name);
                            }
                        }
                    }
                }
                closedir(d);
            }
            dir = strtok(NULL, ":");
        }
        free(path_copy);
    }
}

/* Clean up completion system */
void completions_cleanup(void) {
    for (size_t i = 0; i < num_commands; i++) {
        free(commands[i]);
    }
    free(commands);
    commands = NULL;
    num_commands = 0;
}

/* Helper function to check if a path is a directory */
static int is_directory(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

/* Helper function to escape spaces in filenames */
static char *escape_spaces(const char *str) {
    if (!str) return NULL;
    
    /* Count spaces to determine buffer size */
    size_t spaces = 0;
    for (const char *p = str; *p; p++) {
        if (*p == ' ') spaces++;
    }
    
    /* If no spaces, just return a copy */
    if (spaces == 0) return strdup(str);
    
    /* Allocate buffer for escaped string */
    char *escaped = malloc(strlen(str) + spaces + 1);
    if (!escaped) return NULL;
    
    /* Copy string and escape spaces */
    char *out = escaped;
    for (const char *p = str; *p; p++) {
        if (*p == ' ') {
            *out++ = '\\';
        }
        *out++ = *p;
    }
    *out = '\0';
    
    return escaped;
}

/* Helper function to get directory entries */
static char **get_directory_entries(const char *dir_path, const char *prefix, size_t *count, int dirs_only) {
    char **entries = NULL;
    *count = 0;
    
    DIR *d = opendir(dir_path && dir_path[0] ? dir_path : ".");
    if (!d) return NULL;
    
    size_t prefix_len = prefix ? strlen(prefix) : 0;
    struct dirent *entry;
    
    while ((entry = readdir(d)) != NULL) {
        /* Skip . and .. unless explicitly requested */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            if (!prefix || prefix[0] != '.') continue;
        }
        
        /* Check prefix match */
        if (prefix && strncmp(entry->d_name, prefix, prefix_len) != 0) {
            continue;
        }
        
        /* Construct full path for directory check */
        char full_path[PATH_MAX];
        if (dir_path && dir_path[0]) {
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s", entry->d_name);
        }
        
        /* Check if it's a directory */
        int is_dir = is_directory(full_path);
        
        /* Skip if we only want directories and this isn't one */
        if (dirs_only && !is_dir) continue;
        
        /* Add to entries */
        char **new_entries = realloc(entries, (*count + 1) * sizeof(char*));
        if (new_entries) {
            entries = new_entries;
            /* Add trailing slash for directories */
            char *escaped_name = escape_spaces(entry->d_name);
            if (escaped_name) {
                if (is_dir) {
                    entries[*count] = malloc(strlen(escaped_name) + 2);
                    if (entries[*count]) {
                        strcpy(entries[*count], escaped_name);
                        strcat(entries[*count], "/");
                    }
                } else {
                    entries[*count] = strdup(escaped_name);
                }
                free(escaped_name);
                if (entries[*count]) {
                    (*count)++;
                }
            }
        }
    }
    
    closedir(d);
    return entries;
}

/* Tab completion function */
unsigned char ghost_complete(EditLine *edit_line, int ch) {
    (void)ch;
    const LineInfo *line_info = el_line(edit_line);
    
    /* Find start of current word */
    const char *word_start = line_info->cursor;
    while (word_start > line_info->buffer && !isspace(word_start[-1]) && word_start[-1] != '\\') {
        word_start--;
    }
    
    /* Get word length */
    int len = line_info->cursor - word_start;
    if (len == 0) {
        /* At start of word, just insert a tab if at start of line */
        if (word_start == line_info->buffer) {
            el_insertstr(edit_line, "\t");
            return CC_REDISPLAY;
        }
        /* Otherwise, do nothing */
        return CC_NORM;
    }
    
    /* Get current word and unescape it for matching */
    char *word = malloc(len + 1);
    if (!word) return CC_ERROR;
    
    /* Copy word and handle escaped spaces */
    int j = 0;
    for (int i = 0; i < len; i++) {
        if (word_start[i] == '\\' && i + 1 < len && word_start[i + 1] == ' ') {
            word[j++] = ' ';
            i++; /* Skip the escaped space */
        } else {
            word[j++] = word_start[i];
        }
    }
    word[j] = '\0';
    
    /* Determine completion type */
    int completing_command = (word_start == line_info->buffer);
    int completing_cd = 0;
    
    /* Check if we're completing after 'cd' or starting with ./ */
    if (!completing_command) {
        const char *cmd_start = line_info->buffer;
        while (isspace(*cmd_start)) cmd_start++;
        completing_cd = (strncmp(cmd_start, "cd", 2) == 0 && 
                       (isspace(cmd_start[2]) || cmd_start[2] == '\0'));
    } else if (strncmp(word, "./", 2) == 0) {
        /* If starting with ./, treat as file completion instead of command */
        completing_command = 0;
    }
    
    /* Get directory and file parts for path completion */
    char *dir_part = NULL;
    char *file_part = NULL;
    char *last_slash = strrchr(word, '/');
    
    if (last_slash) {
        if (last_slash == word) {
            dir_part = strdup("/");
            file_part = strdup(last_slash + 1);
        } else {
            dir_part = strndup(word, last_slash - word);
            file_part = strdup(last_slash + 1);
        }
    } else {
        dir_part = strdup(".");
        file_part = strdup(word);
    }
    
    /* Get completions */
    char **matches = NULL;
    size_t num_matches = 0;
    
    if (completing_command) {
        /* Complete commands */
        for (size_t i = 0; i < num_commands; i++) {
            if (strncmp(commands[i], word, len) == 0) {
                char **new_matches = realloc(matches, (num_matches + 1) * sizeof(char*));
                if (new_matches) {
                    matches = new_matches;
                    matches[num_matches++] = strdup(commands[i]);
                }
            }
        }
    } else {
        /* Complete files/directories */
        matches = get_directory_entries(dir_part, file_part, &num_matches, completing_cd);
    }
    
    /* Handle completions */
    if (num_matches == 0) {
        /* No matches */
        free(word);
        free(dir_part);
        free(file_part);
        return CC_ERROR;
    }
    
    /* Find common prefix */
    char *common_prefix = strdup(matches[0]);
    size_t common_len = strlen(common_prefix);
    
    for (size_t i = 1; i < num_matches; i++) {
        size_t j;
        for (j = 0; j < common_len && matches[i][j] == common_prefix[j]; j++);
        common_len = j;
        common_prefix[common_len] = '\0';
    }
    
    /* Show all matches if there's more than one */
    if (num_matches > 1) {
        print_completions_columns(matches, num_matches);
    }
    
    /* Replace current word with completion */
    if (common_prefix[0]) {
        /* Delete current word */
        while (len > 0) {
            el_deletestr(edit_line, 1);
            len--;
        }
        
        /* Insert completion */
        if (last_slash) {
            /* Preserve directory part and handle special cases */
            char *completion = NULL;
            size_t completion_size = 0;
            
            /* Always preserve the original directory structure */
            const char *prefix = dir_part;
            const char *separator = "/";
            completion_size = strlen(prefix) + strlen(separator) + strlen(common_prefix) + 1;
            completion = malloc(completion_size);
            if (completion) {
                snprintf(completion, completion_size, "%s%s%s", prefix, separator, common_prefix);
            }
            
            if (completion) {
                el_insertstr(edit_line, completion);
                free(completion);
            }
        } else {
            el_insertstr(edit_line, common_prefix);
        }
        
        /* Add space after unique command completion */
        if (num_matches == 1 && completing_command) {
            el_insertstr(edit_line, " ");
        }
    }
    
    /* Clean up */
    free(common_prefix);
    for (size_t i = 0; i < num_matches; i++) {
        free(matches[i]);
    }
    free(matches);
    free(word);
    free(dir_part);
    free(file_part);
    
    return CC_REDISPLAY;
}
