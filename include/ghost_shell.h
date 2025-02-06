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
#define GHOST_SHELL_PROMPT "ghost> "
#define GHOST_MAX_INPUT_SIZE 4096
#define GHOST_MAX_ARGS 64
#define GHOST_HISTORY_SIZE 1000

/* Global history state */
extern History *hist;
extern HistEvent ev;

/* Command structure */
typedef struct Command {
    char *name;           /* Command name */
    char **args;         /* Command arguments */
    int arg_count;       /* Number of arguments */
    char *input_file;    /* Input redirection file */
    char *output_file;   /* Output redirection file */
    int append_output;   /* Whether to append to output file */
    char *here_doc;      /* Here document content */
    int background;      /* Run in background flag */
    struct Command *next;  /* Next command in pipeline */
} Command;

/* Shell context structure */
typedef struct ShellContext {
    char *current_dir;    /* Current working directory */
    int exit_flag;        /* Shell exit flag */
    int last_status;      /* Last command exit status */
    char *history_file;   /* Path to history file */
    struct GhostAIContext *ai_ctx; /* Ghost AI context */
    char *last_prompt;    /* Last user prompt for AI analysis */
} ShellContext;

/* Include ghost_ai.h after ShellContext is defined */
#include "ghost_ai.h"

/* Core shell functions */
void shell_init(ShellContext *ctx);
void shell_loop(ShellContext *ctx);
void shell_cleanup(ShellContext *ctx);

/* Command handling */
Command *parse_command(const char *input);
int execute_command(Command *cmd, ShellContext *ctx);
void free_command(Command *cmd);

/* Built-in commands */
int builtin_cd(Command *cmd, ShellContext *ctx);
int builtin_exit(Command *cmd, ShellContext *ctx);
int builtin_help(Command *cmd, ShellContext *ctx);
int builtin_history(Command *cmd, ShellContext *ctx);
int builtin_call(Command *cmd, ShellContext *ctx);
int builtin_export(Command *cmd, ShellContext *ctx);

/* Utility functions */
char *read_line(void);
char **split_line(const char *line, int *count);
void print_error(const char *message);

/* History functions */
void load_history(const char *filename);
void save_history(const char *filename);

/* Readline completion functions */
char *command_generator(const char *text, int state);
char **ghost_completion(const char *text, int start, int end);
void initialize_readline(void);

#endif /* GHOST_SHELL_H */
