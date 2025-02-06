#ifndef GHOST_SHELL_H
#define GHOST_SHELL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <dirent.h>
#include <pwd.h>
#include <ctype.h>
#include <histedit.h>
#include "prompt.h"

#define GHOST_SHELL_VERSION "0.1.0"
#define GHOST_MAX_INPUT_SIZE 4096
#define GHOST_MAX_ARGS 64
#define GHOST_HISTORY_SIZE 1000

/* Global history state */
extern History *hist;
extern HistEvent ev;

/* Command structure */
typedef struct ghost_command {
    char *name;           /* Command name */
    char **args;         /* Command arguments */
    size_t arg_count;    /* Number of arguments */
    char *input_file;    /* Input redirection file */
    char *output_file;   /* Output redirection file */
    int append_output;   /* Whether to append to output file */
    char *here_doc;      /* Here document content */
    int background;      /* Run in background flag */
    struct ghost_command *next;  /* Next command in pipeline */
} ghost_command;

/* Forward declaration for ghost_ai_context */
struct ghost_ai_context;

/* Shell context structure */
typedef struct shell_context {
    char *current_dir;    /* Current working directory */
    int exit_flag;        /* Shell exit flag */
    int last_status;      /* Last command exit status */
    char *history_file;   /* Path to history file */
    struct ghost_ai_context *ai_ctx; /* Ghost AI context */
    char *last_prompt;    /* Last user prompt for AI analysis */
} shell_context;

/* Core shell functions */
void shell_init(shell_context *ctx);
void shell_loop(shell_context *ctx);
void shell_cleanup(shell_context *ctx);

/* Command handling */
ghost_command *parse_command(const char *input);
int execute_command(ghost_command *cmd, shell_context *ctx);
void free_command(ghost_command *cmd);

/* Built-in commands */
int builtin_cd(ghost_command *cmd, shell_context *ctx);
int builtin_exit(ghost_command *cmd, shell_context *ctx);
int builtin_help(ghost_command *cmd, shell_context *ctx);
int builtin_history(ghost_command *cmd, shell_context *ctx);
int builtin_call(ghost_command *cmd, shell_context *ctx);
int builtin_export(ghost_command *cmd, shell_context *ctx);
int builtin_source(ghost_command *cmd, shell_context *ctx);

/* Utility functions */
char *read_line(void);
char **split_line(const char *line, size_t *count);
void print_error(const char *message);

/* History functions */
void load_history(const char *filename);
void save_history(const char *filename);

/* Readline completion functions */
char *command_generator(const char *text, int state);
char **ghost_completion(const char *text, int start, int end);
void initialize_readline(void);

#endif /* GHOST_SHELL_H */
