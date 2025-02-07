#include <stdio.h>
#include <stdlib.h>
#include "ghost_shell.h"
#include "logger.h"
#include <limits.h>

/* Check if we're a login shell based on various criteria */
static int is_login_shell(const char *argv0, int argc, char *argv[]) {
    /* Check if first char of argv[0] is '-' */
    if (argv0[0] == '-') return 1;
    
    /* Check command line args for -l or --login */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--login") == 0) {
            return 1;
        }
    }
    
    /* Check if we're the session leader (initial login shell) */
    pid_t pid = getpid();
    pid_t sid = getsid(0);
    if (pid == sid) return 1;
    
    return 0;
}

int main(int argc, char *argv[]) {
    // Initialize logger first
    if (logger_init() != 0) {
        fprintf(stderr, "Failed to initialize logger\n");
        return 1;
    }
    
    shell_context ctx;
    
    /* Determine shell type */
    int is_login = is_login_shell(argv[0], argc, argv);
    
    /* Initialize the shell */
    shell_init(&ctx);
    
    /* Source appropriate startup files */
    const char *home = getenv("HOME");
    if (home) {
        char profile_path[PATH_MAX];
        char rc_path[PATH_MAX];
        
        snprintf(profile_path, sizeof(profile_path), "%s/.ghsh_profile", home);
        snprintf(rc_path, sizeof(rc_path), "%s/.ghshrc", home);
        
        if (is_login) {
            /* Source profile for login shells */
            ghost_command *profile_cmd = parse_command(". ~/.ghsh_profile");
            if (profile_cmd) {
                execute_command(profile_cmd, &ctx);
                free_command(profile_cmd);
            }
            
            /* Login shells also source rc file */
            ghost_command *rc_cmd = parse_command(". ~/.ghshrc");
            if (rc_cmd) {
                execute_command(rc_cmd, &ctx);
                free_command(rc_cmd);
            }
        } else {
            /* Non-login interactive shells only source rc file */
            ghost_command *rc_cmd = parse_command(". ~/.ghshrc");
            if (rc_cmd) {
                execute_command(rc_cmd, &ctx);
                free_command(rc_cmd);
            }
        }
    }
    
    /* Print welcome message with ASCII art */
    printf("\n");
    printf("   ▄████  ██░ ██  ▒█████    ██████ ▄▄▄█████▓\n");
    printf("  ██▒ ▀█▒▓██░ ██▒▒██▒  ██▒▒██    ▒ ▓  ██▒ ▓▒\n");
    printf(" ▒██░▄▄▄░▒██▀▀██░▒██░  ██▒░ ▓██▄   ▒ ▓██░ ▒░\n");
    printf(" ░▓█  ██▓░▓█ ░██ ▒██   ██░  ▒   ██▒░ ▓██▓ ░ \n");
    printf(" ░▒▓███▀▒░▓█▒░██▓░ ████▓▒░▒██████▒▒  ▒██▒ ░ \n");
    printf("  ░▒   ▒  ▒ ░░▒░▒░ ▒░▒░▒░ ▒ ▒▓▒ ▒ ░  ▒ ░░   \n");
    printf("   ░   ░  ▒ ░▒░ ░  ░ ▒ ▒░ ░ ░▒  ░ ░    ░    \n");
    printf(" ░ ░   ░  ░  ░░ ░░ ░ ░ ▒  ░  ░  ░    ░      \n");
    printf("       ░  ░  ░  ░    ░ ░        ░           \n");
    printf("\n");
    printf("                Shell v%s\n", GHOST_SHELL_VERSION);
    printf("\n");

    /* Check for OpenAI API key */
    if (getenv("OPENAI_API_KEY") == NULL) {
        printf("Error: OPENAI_API_KEY environment variable is not set.\n");
        printf("Please set it using: export OPENAI_API_KEY='your-api-key'\n");
        printf("\n");
    }
    
    /* Main shell loop */
    shell_loop(&ctx);
    
    /* Cleanup before exit */
    shell_cleanup(&ctx);
    
    // Cleanup logger before exit
    logger_cleanup();
    
    return ctx.last_status;
}
