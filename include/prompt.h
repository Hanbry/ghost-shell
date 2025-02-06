#ifndef PROMPT_H
#define PROMPT_H

#include <stdio.h>
#include <stdlib.h>

/* Helper function to format shell prompts */
void format_shell_prompt(char *buffer, size_t size, const char *username, const char *path);

/* Helper function to get current directory with ~ for home */
char *get_formatted_path(void);

#endif /* PROMPT_H */
