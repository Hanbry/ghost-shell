#include "ghost_shell.h"

int main(void) {
    ShellContext ctx;
    
    /* Initialize the shell */
    shell_init(&ctx);
    
    /* Print welcome message with high-tech ASCII art */
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
        printf(COLOR_RED "Error: OPENAI_API_KEY environment variable is not set.\n");
        printf("Please set it using: export OPENAI_API_KEY='your-api-key'\n" COLOR_RESET);
        printf("\n");
    }
    
    /* Main shell loop */
    shell_loop(&ctx);
    
    /* Cleanup before exit */
    shell_cleanup(&ctx);
    
    return ctx.last_status;
}
