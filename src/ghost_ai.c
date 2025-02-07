#include "ghost_shell.h"
#include "ghost_ai.h"
#include "json_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/select.h>
#include <unistd.h>
#include <termios.h>
#include <sys/time.h>
#include <ctype.h>
#include <curl/curl.h>

#define MAX_FOLLOWUP_ATTEMPTS 50

/* Curl write callback */
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    char **response_ptr = (char **)userp;

    if (!contents || !userp || realsize == 0) {
        return 0;
    }

    if (realsize > MAX_RESPONSE_SIZE) {
        fprintf(stderr, "Response too large\n");
        return 0;
    }

    if (*response_ptr == NULL) {
        *response_ptr = malloc(realsize + 1);
        if (*response_ptr == NULL) {
            fprintf(stderr, "Failed to allocate memory in write callback\n");
            return 0;
        }
        memcpy(*response_ptr, contents, realsize);
        (*response_ptr)[realsize] = '\0';
        return realsize;
    }

    size_t current_size = strlen(*response_ptr);
    if (current_size > MAX_RESPONSE_SIZE - realsize) {
        fprintf(stderr, "Response would exceed maximum size\n");
        free(*response_ptr);
        *response_ptr = NULL;
        return 0;
    }

    char *new_ptr = realloc(*response_ptr, current_size + realsize + 1);
    if (new_ptr == NULL) {
        fprintf(stderr, "Failed to reallocate memory in write callback\n");
        free(*response_ptr);
        *response_ptr = NULL;
        return 0;
    }

    *response_ptr = new_ptr;
    memcpy(*response_ptr + current_size, contents, realsize);
    (*response_ptr)[current_size + realsize] = '\0';
    return realsize;
}

/* Initialize conversation history */
static conversation_history *init_conversation_history(void) {
    conversation_history *history = calloc(1, sizeof(conversation_history));
    if (!history) return NULL;
    history->head = NULL;
    history->tail = NULL;
    history->message_count = 0;
    return history;
}

/* Add a message to the conversation history */
void ghost_ai_add_to_history(ghost_ai_context *ai_ctx, message_type type, const char *content) {
    if (!ai_ctx || !content || !ai_ctx->history) return;
    conversation_message *msg = calloc(1, sizeof(conversation_message));
    if (!msg) return;
    size_t content_len = strlen(content);
    if (content_len > MAX_MESSAGE_SIZE) {
        content_len = MAX_MESSAGE_SIZE;
    }
    msg->content = malloc(content_len + 1);
    if (!msg->content) {
        free(msg);
        return;
    }
    strncpy(msg->content, content, content_len);
    msg->content[content_len] = '\0';
    msg->type = type;
    msg->next = NULL;

    if (!ai_ctx->history->head) {
        ai_ctx->history->head = msg;
        ai_ctx->history->tail = msg;
    } else {
        ai_ctx->history->tail->next = msg;
        ai_ctx->history->tail = msg;
    }
    ai_ctx->history->message_count++;

    if (ai_ctx->history->message_count > MAX_HISTORY_MESSAGES) {
        ghost_ai_trim_history(ai_ctx);
    }
}

/* Trim history to maximum size by removing oldest messages */
void ghost_ai_trim_history(ghost_ai_context *ai_ctx) {
    if (!ai_ctx || !ai_ctx->history || !ai_ctx->history->head) return;
    while (ai_ctx->history->message_count > MAX_HISTORY_MESSAGES && ai_ctx->history->head) {
        conversation_message *old_head = ai_ctx->history->head;
        ai_ctx->history->head = old_head->next;
        if (old_head->content) free(old_head->content);
        free(old_head);
        ai_ctx->history->message_count--;
    }
    if (!ai_ctx->history->head) {
        ai_ctx->history->tail = NULL;
    }
}

/* Clear all history */
void ghost_ai_clear_history(ghost_ai_context *ai_ctx) {
    if (!ai_ctx || !ai_ctx->history) return;
    conversation_message *current = ai_ctx->history->head;
    while (current) {
        conversation_message *next = current->next;
        if (current->content) free(current->content);
        free(current);
        current = next;
    }
    ai_ctx->history->head = NULL;
    ai_ctx->history->tail = NULL;
    ai_ctx->history->message_count = 0;
}

/* Initialize the AI context */
ghost_ai_context *ghost_ai_init(void) {
    ghost_ai_context *ctx = calloc(1, sizeof(ghost_ai_context));
    if (!ctx) return NULL;
    ctx->api_key = NULL;
    ctx->system_prompt = NULL;
    ctx->last_response = NULL;
    ctx->is_ghost_mode = 0;
    ctx->history = init_conversation_history();
    if (!ctx->history) {
        ghost_ai_cleanup(ctx);
        return NULL;
    }
    const char *api_key = getenv("OPENAI_API_KEY");
    if (!api_key) {
        ghost_ai_cleanup(ctx);
        return NULL;
    }
    if (strncmp(api_key, "sk-", 3) != 0) {
        ghost_ai_cleanup(ctx);
        return NULL;
    }
    size_t key_len = strlen(api_key);
    char *cleaned_key = malloc(key_len + 1);
    if (!cleaned_key) {
        ghost_ai_cleanup(ctx);
        return NULL;
    }
    size_t j = 0;
    for (size_t i = 0; i < key_len; i++) {
        if (api_key[i] >= 32 && api_key[i] <= 126) {
            cleaned_key[j++] = api_key[i];
        }
    }
    cleaned_key[j] = '\0';
    if (strlen(cleaned_key) < 5) {
        free(cleaned_key);
        ghost_ai_cleanup(ctx);
        return NULL;
    }
    ctx->api_key = cleaned_key;
    const char *system_prompt = 
        "You are a shell command executor. "
        "You MUST ONLY output raw shell commands. "
        "NEVER use markdown formatting, code blocks, or ``` markers. "
        "NEVER include explanations or comments. "
        "NEVER return partial commands, they must be complete and executable. "
        "Every line you output will be executed directly in the shell. "
        "When you need to create a file, use echo with proper shell quoting and redirection. "
        "If a task needs multiple steps, use shell operators (;, &&, |) or execute them one by one. "
        "When analyzing output, only respond with 'SUCCESS' if the task is complete.";
    ctx->system_prompt = strdup(system_prompt);
    if (!ctx->system_prompt) {
        ghost_ai_cleanup(ctx);
        return NULL;
    }
    return ctx;
}

/* Clean up the AI context */
void ghost_ai_cleanup(ghost_ai_context *ai_ctx) {
    if (!ai_ctx) return;
    if (ai_ctx->history) {
        ghost_ai_clear_history(ai_ctx);
        free(ai_ctx->history);
        ai_ctx->history = NULL;
    }
    if (ai_ctx->api_key) {
        memset(ai_ctx->api_key, 0, strlen(ai_ctx->api_key));
        free(ai_ctx->api_key);
        ai_ctx->api_key = NULL;
    }
    if (ai_ctx->system_prompt) {
        free(ai_ctx->system_prompt);
        ai_ctx->system_prompt = NULL;
    }
    if (ai_ctx->last_response) {
        free(ai_ctx->last_response);
        ai_ctx->last_response = NULL;
    }
    memset(ai_ctx, 0, sizeof(ghost_ai_context));
    free(ai_ctx);
}

/* Simple JSON string escaping */
static char *escape_json_string(const char *str) {
    size_t len = strlen(str);
    char *result = malloc(len * 2 + 1);  /* Worst case: every char needs escaping */
    if (!result) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        switch (str[i]) {
            case '\\': result[j++] = '\\'; result[j++] = '\\'; break;
            case '"':  result[j++] = '\\'; result[j++] = '"'; break;
            case '\n': result[j++] = '\\'; result[j++] = 'n'; break;
            case '\r': result[j++] = '\\'; result[j++] = 'r'; break;
            case '\t': result[j++] = '\\'; result[j++] = 't'; break;
            default:   result[j++] = str[i];
        }
    }
    result[j] = '\0';
    return result;
}

/* Process a prompt through the AI and optionally execute commands if in ghost mode */
int ghost_ai_process(const char *prompt, ghost_ai_context *ai_ctx, struct shell_context *shell_ctx) {
    if (!prompt || !ai_ctx || !shell_ctx) {
        return 1;
    }

    CURL *curl = NULL;
    CURLcode res;
    char *response = NULL;
    char *payload = NULL;
    char *escaped_system = NULL;
    struct curl_slist *headers = NULL;
    int result = 1;  /* Default to error */

    payload = malloc(MAX_RESPONSE_SIZE);
    if (!payload) {
        fprintf(stderr, "Failed to allocate payload buffer\n");
        goto cleanup;
    }

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Failed to initialize curl\n");
        goto cleanup;
    }

    escaped_system = escape_json_string(ai_ctx->system_prompt);
    if (!escaped_system) {
        fprintf(stderr, "Failed to escape system prompt\n");
        goto cleanup;
    }

    /* Add user message to history */
    ghost_ai_add_to_history(ai_ctx, MESSAGE_USER, prompt);

    /* Build messages array from history */
    conversation_message *current = ai_ctx->history->head;
    size_t messages_json_size = 1024;
    char *messages_json = malloc(messages_json_size);
    if (!messages_json) goto cleanup;
    strcpy(messages_json, "[");
    size_t json_len = 1;

    /* Add system message first */
    json_len += snprintf(messages_json + json_len, messages_json_size - json_len,
                           "{\"role\":\"system\",\"content\":\"%s\"}", escaped_system);
    free(escaped_system);

    /* Add messages from history */
    while (current) {
        const char *role = NULL;
        switch (current->type) {
            case MESSAGE_USER: role = "user"; break;
            case MESSAGE_ASSISTANT: role = "assistant"; break;
            case MESSAGE_COMMAND_OUTPUT: role = "user"; break;
            default: current = current->next; continue;
        }

        char *escaped_content = escape_json_string(current->content);
        if (!escaped_content) {
            current = current->next;
            continue;
        }

        size_t needed = json_len + strlen(escaped_content) + 64;
        if (needed > messages_json_size) {
            messages_json_size = needed * 2;
            char *new_buffer = realloc(messages_json, messages_json_size);
            if (!new_buffer) {
                free(escaped_content);
                goto cleanup;
            }
            messages_json = new_buffer;
        }

        json_len += snprintf(messages_json + json_len, messages_json_size - json_len,
                             ",{\"role\":\"%s\",\"content\":\"%s\"}", role, escaped_content);
        free(escaped_content);
        current = current->next;
    }
    strcat(messages_json, "]");

    int written = snprintf(payload, MAX_RESPONSE_SIZE,
                           "{\"model\":\"%s\",\"messages\":%s}",
                           OPENAI_MODEL, messages_json);
    free(messages_json);
    if (written >= MAX_RESPONSE_SIZE) {
        fprintf(stderr, "Payload truncated, increase MAX_RESPONSE_SIZE\n");
        goto cleanup;
    }

    char *auth_header = malloc(strlen(ai_ctx->api_key) + 22);  /* "Authorization: Bearer " + key + \0 */
    
    if (!auth_header) goto cleanup;
    
    sprintf(auth_header, "Authorization: Bearer %s", ai_ctx->api_key);
    headers = curl_slist_append(headers, auth_header);
    free(auth_header);
    
    if (!headers) goto cleanup;
    
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    if (!headers) goto cleanup;

    curl_easy_setopt(curl, CURLOPT_URL, OPENAI_API_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        goto cleanup;
    }
    if (!response || !response[0]) {
        fprintf(stderr, "Empty response from AI\n");
        goto cleanup;
    }

    /* Parse JSON response */
    char *content = parse_ai_response_content(response);
    if (!content) {
        fprintf(stderr, "Failed to parse AI response\n");
        goto cleanup;
    }

    /* Save AI response for later analysis */
    if (ai_ctx->last_response) {
        free(ai_ctx->last_response);
    }
    ai_ctx->last_response = strdup(content);

    /* If in ghost mode, parse and execute commands */
    if (ai_ctx->is_ghost_mode) {
        size_t cmd_count;
        char **commands = ghost_ai_parse_commands(content, &cmd_count);
        if (commands && cmd_count > 0) {
            ghost_ai_execute_commands(commands, cmd_count, shell_ctx);
            for (size_t i = 0; i < cmd_count; i++) {
                if (commands[i]) free(commands[i]);
            }
            free(commands);
            result = 0;
        }
    } else {
        result = 0;
    }
    free(content);

    ghost_ai_add_to_history(ai_ctx, MESSAGE_ASSISTANT, ai_ctx->last_response ? ai_ctx->last_response : "");

cleanup:
    if (headers) curl_slist_free_all(headers);
    if (curl) curl_easy_cleanup(curl);
    if (payload) free(payload);
    if (response) free(response);
    return result;
}

/* Iteratively analyze command output and follow up until success or max attempts reached */
int ghost_ai_analyze_and_followup(const char *original_prompt, const char *command_output,
                                    ghost_ai_context *ai_ctx, struct shell_context *shell_ctx) {
    int attempt = 0;
    int result = 1;

    while (attempt < MAX_FOLLOWUP_ATTEMPTS) {
        size_t prompt_size = strlen(original_prompt) + strlen(command_output) + 300;
        char *analysis_prompt = malloc(prompt_size);
        if (!analysis_prompt) {
            fprintf(stderr, "Failed to allocate analysis prompt buffer\n");
            return 1;
        }

        snprintf(analysis_prompt, prompt_size,
                 "The user requested: '%s'\n"
                 "The command output was:\n%s\n"
                 "Please analyze if this output satisfies the user's request. "
                 "If it is correct and complete, respond with only 'SUCCESS'. "
                 "If it is not correct or incomplete, explain what needs to be done.",
                 original_prompt, command_output);

        int saved_mode = ai_ctx->is_ghost_mode;
        ai_ctx->is_ghost_mode = 0;
        result = ghost_ai_process(analysis_prompt, ai_ctx, shell_ctx);
        ai_ctx->is_ghost_mode = saved_mode;
        free(analysis_prompt);

        if (ai_ctx->last_response == NULL) {
            break;
        }
        if (strstr(ai_ctx->last_response, "SUCCESS") != NULL) {
            result = 0;
            break;
        }

        prompt_size = strlen(original_prompt) + strlen(command_output) +
                      strlen(ai_ctx->last_response) + 300;
        char *followup_prompt = malloc(prompt_size);

        if (!followup_prompt) {
            fprintf(stderr, "Failed to allocate follow-up prompt buffer\n");
            result = 1;
            break;
        }

        snprintf(followup_prompt, prompt_size,
                 "The user requested: '%s'\n"
                 "The previous attempt resulted in:\n%s\n"
                 "Your analysis indicated the following issues:\n%s\n"
                 "Please provide the commands needed to fulfill the request correctly. "
                 "ONLY provide valid, complete shell commands.",
                 original_prompt, command_output, ai_ctx->last_response);
        result = ghost_ai_process(followup_prompt, ai_ctx, shell_ctx);
        free(followup_prompt);

        if (ai_ctx->last_response && strstr(ai_ctx->last_response, "SUCCESS") != NULL) {
            result = 0;
            break;
        }

        attempt++;
    }

    return result;
}

/* Parse AI response into individual commands (one per non-empty line) */
char **ghost_ai_parse_commands(const char *ai_response, size_t *cmd_count) {
    if (!ai_response || !cmd_count) return NULL;

    char *response_copy = strdup(ai_response);
    
    if (!response_copy) return NULL;
    
    char **commands = NULL;
    *cmd_count = 0;
    char *ptr = response_copy;
    char *line_start = ptr;
    
    while (ptr && *ptr) {
        if (*ptr == '\n' || *ptr == '\0') {
            char tmp = *ptr;
            *ptr = '\0';
            if (line_start && strlen(line_start) > 0) {
                (*cmd_count)++;
            }
            *ptr = tmp;
            line_start = ptr + 1;
        }
        ptr++;
    }
    
    if (line_start && *line_start && strlen(line_start) > 0) {
        (*cmd_count)++;
    }
    
    if (*cmd_count == 0) {
        free(response_copy);
        return NULL;
    }
    
    commands = calloc(*cmd_count + 1, sizeof(char*));
    
    if (!commands) {
        free(response_copy);
        return NULL;
    }
    
    char *saveptr = NULL;
    char *line = strtok_r(response_copy, "\n", &saveptr);
    size_t i = 0;
    
    while (line && i < *cmd_count) {
        while (line && *line && isspace(*line)) line++;
    
        if (line && *line) {
            char *end = line + strlen(line) - 1;
            while (end > line && isspace(*end)) end--;
            *(end + 1) = '\0';
            if (strlen(line) > 0) {
                commands[i] = strdup(line);
                if (!commands[i]) {
                    for (size_t j = 0; j < i; j++) {
                        free(commands[j]);
                    }
                    free(commands);
                    free(response_copy);
                    return NULL;
                }
                i++;
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    
    *cmd_count = i;
    commands[i] = NULL;
    free(response_copy);
    return commands;
}

/* Display the command with a typing effect allowing the user to modify it */
void ghost_ai_display_command(const char *command, char *modified_command, size_t modified_size) {
    if (!command || !modified_command) return;
    
    size_t len = strlen(command);
    
    if (len == 0) return;
    
    struct termios old_term, new_term;
    
    if (tcgetattr(STDIN_FILENO, &old_term) == -1) return;
    
    new_term = old_term;
    new_term.c_lflag &= ~(ICANON | ECHO);
    new_term.c_cc[VMIN] = 1;
    new_term.c_cc[VTIME] = 0;
    
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
    strncpy(modified_command, command, modified_size - 1);
    modified_command[modified_size - 1] = '\0';
    
    size_t pos = strlen(modified_command);
    printf("\r\033[K");
    static char prompt[1024];
    char *path = get_formatted_path();
    format_shell_prompt(prompt, sizeof(prompt), "ghost", path);
    printf("%s%s", prompt, modified_command);
    fflush(stdout);
    free(path);

    struct timeval start_tv, current_tv;
    gettimeofday(&start_tv, NULL);
    
    while (1) {
        gettimeofday(&current_tv, NULL);
        long elapsed_usec = (current_tv.tv_sec - start_tv.tv_sec) * 1000000L +
                              (current_tv.tv_usec - start_tv.tv_usec);
        if (elapsed_usec >= 1500000) {
            break;
        }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
            char c;
            if (read(STDIN_FILENO, &c, 1) > 0) {
                if (c == 127 || c == 8) { /* Backspace */
                    if (pos > 0) {
                        pos--;
                        modified_command[pos] = '\0';
                        printf("\r\033[K");
                        path = get_formatted_path();
                        format_shell_prompt(prompt, sizeof(prompt), "ghost", path);
                        printf("%s%s", prompt, modified_command);
                        free(path);
                        fflush(stdout);
                    }
                }
                else if (c >= 32 && c <= 126 && pos < modified_size - 1) {
                    modified_command[pos++] = c;
                    modified_command[pos] = '\0';
                    printf("\r\033[K");
                    path = get_formatted_path();
                    format_shell_prompt(prompt, sizeof(prompt), "ghost", path);
                    printf("%s%s", prompt, modified_command);
                    free(path);
                    fflush(stdout);
                }
            }
        }
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    printf("\n");
    fflush(stdout);
}

/* Capture the output of a command execution */
char *ghost_ai_capture_command_output(const char *command, ghost_ai_context *ai_ctx) {
    FILE *fp;
    char *output = NULL;
    size_t total_size = 0;
    char buffer[4096];

    fp = popen(command, "r");
    if (fp == NULL) {
        fprintf(stderr, "Failed to run command: %s\n", command);
        return NULL;
    }

    while (!feof(fp)) {
        size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, fp);
        if (bytes_read > 0) {
            char *new_output = realloc(output, total_size + bytes_read + 1);
            if (new_output == NULL) {
                free(output);
                pclose(fp);
                return NULL;
            }
            output = new_output;
            memcpy(output + total_size, buffer, bytes_read);
            total_size += bytes_read;
            output[total_size] = '\0';
        }
    }

    pclose(fp);

    if (output && ai_ctx && ai_ctx->history) {
        ghost_ai_add_to_history(ai_ctx, MESSAGE_COMMAND_OUTPUT, output);
    }

    return output;
}

/* Execute an array of commands and analyze the output iteratively */
void ghost_ai_execute_commands(char **commands, size_t cmd_count, struct shell_context *shell_ctx) {
    if (!commands || !shell_ctx || cmd_count == 0) return;
    
    for (size_t i = 0; i < cmd_count; i++) {
        char modified_command[4096];
        char *output = NULL;
        ghost_ai_display_command(commands[i], modified_command, sizeof(modified_command));
        output = ghost_ai_capture_command_output(modified_command, shell_ctx->ai_ctx);
        if (output) {
            printf("%s", output);
            if (ghost_ai_analyze_and_followup(shell_ctx->last_prompt, output,
                                              shell_ctx->ai_ctx, shell_ctx) == 0) {
                free(output);
                break;
            }
            free(output);
        }
    }
}
