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

/* Check if GNU readline was explicitly requested */
#ifdef USE_GNU_READLINE
    #include <readline/readline.h>
    #include <readline/history.h>
#else
    /* Default to editline (BSD licensed) */
    #include <editline/readline.h>
    #include <histedit.h>
#endif

/* ANSI Color and Style codes */
#define COLOR_RESET     "\033[0m"
#define COLOR_BOLD      "\033[1m"

/* Regular colors */
#define COLOR_RED       "\033[31m"
#define COLOR_GREEN     "\033[32m"
#define COLOR_YELLOW    "\033[33m"
#define COLOR_BLUE      "\033[34m"
#define COLOR_MAGENTA   "\033[35m"
#define COLOR_CYAN      "\033[36m"
#define COLOR_WHITE     "\033[37m"

/* Bright colors */
#define COLOR_BRIGHT_RED     "\033[91m"
#define COLOR_BRIGHT_GREEN   "\033[92m"
#define COLOR_BRIGHT_YELLOW  "\033[93m"
#define COLOR_BRIGHT_BLUE    "\033[94m"
#define COLOR_BRIGHT_MAGENTA "\033[95m"
#define COLOR_BRIGHT_CYAN    "\033[96m"
#define COLOR_BRIGHT_WHITE   "\033[97m"

#define GHOST_SHELL_VERSION "0.1.0"
#define GHOST_SHELL_PROMPT "ghost> "
#define GHOST_MAX_INPUT_SIZE 4096
#define GHOST_MAX_ARGS 64
#define GHOST_HISTORY_SIZE 1000

/* Global history state */
#ifndef USE_GNU_READLINE
extern History *hist;
extern HistEvent ev;
#endif

/* Command structure */
typedef struct {
    char *name;           /* Command name */
    char **args;         /* Command arguments */
    int arg_count;       /* Number of arguments */
    char *input_file;    /* Input redirection file */
    char *output_file;   /* Output redirection file */
    int background;      /* Run in background flag */
} Command;

/* Shell context structure */
typedef struct {
    char *current_dir;    /* Current working directory */
    int exit_flag;        /* Shell exit flag */
    int last_status;      /* Last command exit status */
    char *history_file;   /* Path to history file */
} ShellContext;

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