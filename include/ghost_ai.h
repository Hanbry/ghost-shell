#ifndef GHOST_AI_H
#define GHOST_AI_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>

/* Forward declaration */
struct shell_context;

/* OpenAI API configuration */
#define OPENAI_API_URL "https://api.openai.com/v1/chat/completions"
#define OPENAI_MODEL "gpt-4o"
#define MAX_RESPONSE_SIZE 16384

/* Ghost AI context structure */
typedef struct ghost_ai_context {
    char *api_key;           /* OpenAI API key */
    char *system_prompt;     /* System prompt for the AI */
    char *last_response;     /* Last AI response */
    int is_ghost_mode;       /* Whether we're in ghost mode */
} ghost_ai_context;

/* Ghost AI functions */
ghost_ai_context *ghost_ai_init(void);
void ghost_ai_cleanup(ghost_ai_context *ai_ctx);
int ghost_ai_process(const char *prompt, ghost_ai_context *ai_ctx, struct shell_context *shell_ctx);
char **ghost_ai_parse_commands(const char *ai_response, int *cmd_count);
int ghost_ai_analyze_output(const char *original_prompt, const char *command_output, ghost_ai_context *ai_ctx, struct shell_context *shell_ctx);
int ghost_ai_handle_followup(const char *original_prompt, const char *command_output, const char *analysis_response, ghost_ai_context *ai_ctx, struct shell_context *shell_ctx);

/* Helper functions */
void ghost_ai_display_command(const char *command, char *modified_command, size_t modified_size);
void ghost_ai_execute_commands(char **commands, int cmd_count, struct shell_context *shell_ctx);
char *ghost_ai_capture_command_output(const char *command, struct shell_context *shell_ctx);

#endif /* GHOST_AI_H */
