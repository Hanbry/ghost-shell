#include "prompt.h"
#include <string.h>
#include <unistd.h>
#include <libgen.h>

/* Helper function to format shell prompts */
void format_shell_prompt(char *buffer, size_t size, const char *username, const char *path) {
    if (!buffer || !username || !path) return;
    snprintf(buffer, size, "%s@ghsh %s > ", username, path);
}

/* Helper function to get current directory with ~ for home */
char *get_formatted_path(void) {
    char *cwd = getcwd(NULL, 0);
    if (!cwd) return strdup("???");

    const char *home = getenv("HOME");
    char *result = NULL;
    
    /* Case 1: home directory itself */
    if (home && strcmp(cwd, home) == 0) {
        result = strdup("~");
        free(cwd);
        return result;
    }
    
    /* Make copies for dirname/basename as they may modify the string */
    char *dir_copy = strdup(cwd);
    char *base_copy = strdup(cwd);
    if (!dir_copy || !base_copy) {
        free(cwd);
        free(dir_copy);
        free(base_copy);
        return strdup("???");
    }
    
    char *dir = dirname(dir_copy);
    char *base = basename(base_copy);
    
    /* Case 2: Top-level directory (parent is root) */
    if (strcmp(dir, "/") == 0) {
        result = strdup(cwd);
    }
    /* Case 3: All other directories: just show the last component */
    else {
        result = strdup(base);
    }
    
    free(dir_copy);
    free(base_copy);
    free(cwd);
    return result ? result : strdup("???");
} 
