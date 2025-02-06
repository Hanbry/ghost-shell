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
#define MAX_HISTORY_MESSAGES 50  /* Maximum number of messages to keep in history */
#define MAX_MESSAGE_SIZE 8192    /* Maximum size of a single message */

/* Message types in conversation */
typedef enum {
    MESSAGE_SYSTEM,
    MESSAGE_USER,
    MESSAGE_ASSISTANT,
    MESSAGE_COMMAND_OUTPUT
} message_type;

/* Single message in conversation history */
typedef struct conversation_message {
    message_type type;
    char *content;
    struct conversation_message *next;
} conversation_message;

/* Conversation history */
typedef struct conversation_history {
    conversation_message *head;
    conversation_message *tail;
    size_t message_count;
} conversation_history;

/* Ghost AI context structure */
typedef struct ghost_ai_context {
    char *api_key;           /* OpenAI API key */
    char *system_prompt;     /* System prompt for the AI */
    char *last_response;     /* Last AI response */
    int is_ghost_mode;       /* Whether we're in ghost mode */
    conversation_history *history;  /* Conversation history */
} ghost_ai_context;

/* Ghost AI functions */
ghost_ai_context *ghost_ai_init(void);
void ghost_ai_cleanup(ghost_ai_context *ai_ctx);
int ghost_ai_process(const char *prompt, ghost_ai_context *ai_ctx, struct shell_context *shell_ctx);
char **ghost_ai_parse_commands(const char *ai_response, size_t *cmd_count);
int ghost_ai_analyze_output(const char *original_prompt, const char *command_output, ghost_ai_context *ai_ctx, struct shell_context *shell_ctx);
int ghost_ai_handle_followup(const char *original_prompt, const char *command_output, const char *analysis_response, ghost_ai_context *ai_ctx, struct shell_context *shell_ctx);

/* History management functions */
void ghost_ai_add_to_history(ghost_ai_context *ai_ctx, message_type type, const char *content);
void ghost_ai_trim_history(ghost_ai_context *ai_ctx);
void ghost_ai_clear_history(ghost_ai_context *ai_ctx);

/* Helper functions */
void ghost_ai_display_command(const char *command, char *modified_command, size_t modified_size);
void ghost_ai_execute_commands(char **commands, size_t cmd_count, struct shell_context *shell_ctx);
char *ghost_ai_capture_command_output(const char *command, ghost_ai_context *ai_ctx);

#endif /* GHOST_AI_H */
