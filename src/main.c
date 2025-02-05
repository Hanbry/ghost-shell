#include "ghost_shell.h"

int main(void) {
    ShellContext ctx;
    
    /* Initialize the shell */
    shell_init(&ctx);
    
    /* Print welcome message */
    printf("Ghost Shell v%s\n", GHOST_SHELL_VERSION);
    printf("Type 'help' for a list of built-in commands.\n\n");
    
    /* Main shell loop */
    shell_loop(&ctx);
    
    /* Cleanup before exit */
    shell_cleanup(&ctx);
    
    return ctx.last_status;
} 