#include "prompt.h"
#include <string.h>
#include <unistd.h>

/* Helper function to format shell prompts */
void format_shell_prompt(char *buffer, size_t size, const char *username, const char *path) {
    if (!buffer || !username || !path) return;
    snprintf(buffer, size, "%s@ghsh:%s$ ", username, path);
}

/* Helper function to get current directory with ~ for home */
char *get_formatted_path(void) {
    char *cwd = getcwd(NULL, 0);
    if (!cwd) return strdup("???");

    const char *dir = cwd;
    const char *home = getenv("HOME");
    static char tilde_path[1024];

    if (home && strncmp(cwd, home, strlen(home)) == 0) {
        if (cwd[strlen(home)] == '\0') {
            dir = "~";
        } else if (cwd[strlen(home)] == '/') {
            snprintf(tilde_path, sizeof(tilde_path), "~%s", cwd + strlen(home));
            dir = tilde_path;
        }
    }

    char *result = strdup(dir);
    free(cwd);
    return result;
} 
