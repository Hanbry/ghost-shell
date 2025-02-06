#include "ghost_shell.h"
#include "ghost_ai.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/select.h>
#include <unistd.h>
#include <termios.h>

/* Curl write callback */
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    char **response_ptr = (char **)userp;
    
    /* Validate input */
    if (!contents || !userp || realsize == 0) {
        return 0;
    }
    
    /* Check for overflow */
    if (realsize > MAX_RESPONSE_SIZE) {
        fprintf(stderr, "Response too large\n");
        return 0;
    }
    
    /* If first allocation */
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
    
    /* For subsequent chunks */
    size_t current_size = strlen(*response_ptr);
    
    /* Check for overflow */
    if (current_size > MAX_RESPONSE_SIZE - realsize) {
        fprintf(stderr, "Response would exceed maximum size\n");
        free(*response_ptr);
        *response_ptr = NULL;
        return 0;
    }
    
    /* Reallocate buffer */
    char *new_ptr = realloc(*response_ptr, current_size + realsize + 1);
    if (new_ptr == NULL) {
        fprintf(stderr, "Failed to reallocate memory in write callback\n");
        free(*response_ptr);
        *response_ptr = NULL;
        return 0;
    }
    
    /* Copy new data */
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
    
    /* Create new message */
    conversation_message *msg = calloc(1, sizeof(conversation_message));
    if (!msg) return;
    
    /* Copy content with size limit */
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
    
    /* Add to history */
    if (!ai_ctx->history->head) {
        ai_ctx->history->head = msg;
        ai_ctx->history->tail = msg;
    } else {
        ai_ctx->history->tail->next = msg;
        ai_ctx->history->tail = msg;
    }
    
    ai_ctx->history->message_count++;
    
    /* Trim history if needed */
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
        
        if (old_head->content) {
            free(old_head->content);
        }
        free(old_head);
        
        ai_ctx->history->message_count--;
    }
    
    /* Update tail if we emptied the history */
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
        if (current->content) {
            free(current->content);
        }
        free(current);
        current = next;
    }
    
    ai_ctx->history->head = NULL;
    ai_ctx->history->tail = NULL;
    ai_ctx->history->message_count = 0;
}

ghost_ai_context *ghost_ai_init(void) {
    ghost_ai_context *ctx = NULL;
    const char *api_key = NULL;
    const char *system_prompt = NULL;
    
    /* Allocate context */
    ctx = calloc(1, sizeof(ghost_ai_context));
    if (!ctx) {
        return NULL;
    }
    
    /* Initialize all fields to NULL */
    ctx->api_key = NULL;
    ctx->system_prompt = NULL;
    ctx->last_response = NULL;
    ctx->is_ghost_mode = 0;
    
    /* Initialize conversation history */
    ctx->history = init_conversation_history();
    if (!ctx->history) {
        ghost_ai_cleanup(ctx);
        return NULL;
    }
    
    /* Get API key from environment */
    api_key = getenv("OPENAI_API_KEY");
    if (!api_key) {
        ghost_ai_cleanup(ctx);
        return NULL;
    }

    /* Validate API key format */
    if (strncmp(api_key, "sk-", 3) != 0) {
        ghost_ai_cleanup(ctx);
        return NULL;
    }

    /* Clean and copy API key - remove any whitespace or hidden characters */
    size_t key_len = strlen(api_key);
    char *cleaned_key = malloc(key_len + 1);
    if (!cleaned_key) {
        ghost_ai_cleanup(ctx);
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < key_len; i++) {
        /* Only copy printable characters */
        if (api_key[i] >= 32 && api_key[i] <= 126) {
            cleaned_key[j++] = api_key[i];
        }
    }
    cleaned_key[j] = '\0';

    /* Verify we still have a valid key after cleaning */
    if (strlen(cleaned_key) < 5) {  /* Minimum length for a valid key */
        free(cleaned_key);
        ghost_ai_cleanup(ctx);
        return NULL;
    }
    
    ctx->api_key = cleaned_key;
    
    /* Set up system prompt */
    system_prompt = 
        "You are a helpful AI assistant in a shell environment. "
        "When the user asks you something, respond with one or more shell commands "
        "that will help answer their question or accomplish their task. "
        "Provide only the commands, one per line, without any additional explanation or formatting. "
        "If a task involves multiple steps, use shell redirection or piping to combine them. "
        "If a task involves multiple commands combine them into one line. "
        "Use only standard Unix commands and tools that are likely to be available. "
        "When analyzing command output, if successful respond with only 'SUCCESS' (not as a command). "
        "If not successful, explain what needs to be done differently.";
    
    ctx->system_prompt = strdup(system_prompt);
    if (!ctx->system_prompt) {
        ghost_ai_cleanup(ctx);
        return NULL;
    }
    
    return ctx;
}

void ghost_ai_cleanup(ghost_ai_context *ai_ctx) {
    if (!ai_ctx) return;
    
    if (ai_ctx->history) {
        ghost_ai_clear_history(ai_ctx);
        free(ai_ctx->history);
        ai_ctx->history = NULL;
    }
    
    if (ai_ctx->api_key) {
        /* Securely clear API key from memory */
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
    
    /* Clear the entire structure before freeing */
    memset(ai_ctx, 0, sizeof(ghost_ai_context));
    free(ai_ctx);
}

/* Simple JSON string escaping */
static char *escape_json_string(const char *str) {
    size_t len = strlen(str);
    char *result = malloc(len * 2 + 1);  /* Worst case: every char needs escaping */
    size_t j = 0;
    
    if (!result) return NULL;
    
    for (size_t i = 0; i < len; i++) {
        switch (str[i]) {
            case '\\': result[j++] = '\\'; result[j++] = '\\'; break;
            case '"':  result[j++] = '\\'; result[j++] = '"';  break;
            case '\n': result[j++] = '\\'; result[j++] = 'n';  break;
            case '\r': result[j++] = '\\'; result[j++] = 'r';  break;
            case '\t': result[j++] = '\\'; result[j++] = 't';  break;
            default:   result[j++] = str[i];
        }
    }
    result[j] = '\0';
    return result;
}

int ghost_ai_process(const char *prompt, ghost_ai_context *ai_ctx, struct shell_context *shell_ctx) {
    if (!prompt || !ai_ctx || !shell_ctx) {
        return 1;
    }

    CURL *curl = NULL;
    CURLcode res;
    char *response = NULL;
    char *payload = NULL;
    char *escaped_prompt = NULL;
    char *escaped_system = NULL;
    struct curl_slist *headers = NULL;
    int result = 1;  /* Default to error */
    
    /* Allocate payload buffer */
    payload = malloc(MAX_RESPONSE_SIZE);
    if (!payload) {
        goto cleanup;
    }
    
    /* Initialize curl */
    curl = curl_easy_init();
    if (!curl) {
        goto cleanup;
    }
    
    /* Escape strings for JSON */
    escaped_prompt = escape_json_string(prompt);
    escaped_system = escape_json_string(ai_ctx->system_prompt);
    
    if (!escaped_prompt || !escaped_system) {
        goto cleanup;
    }
    
    /* Add user message to history */
    ghost_ai_add_to_history(ai_ctx, MESSAGE_USER, prompt);
    
    /* Build messages array from history */
    conversation_message *current = ai_ctx->history->head;
    size_t messages_json_size = 1024;  /* Start with some buffer */
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
            default: continue;  /* Skip other types */
        }
        
        char *escaped_content = escape_json_string(current->content);
        if (!escaped_content) continue;
        
        /* Check if we need more buffer space */
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
    
    strcat(messages_json + json_len, "]");
    
    /* Prepare JSON payload */
    int written = snprintf(payload, MAX_RESPONSE_SIZE,
                         "{\"model\":\"%s\",\"messages\":%s}",
                         OPENAI_MODEL, messages_json);
    
    free(messages_json);
    
    if (written >= MAX_RESPONSE_SIZE) {
        goto cleanup;
    }
    
    /* Set up headers */
    char *auth_header = malloc(strlen(ai_ctx->api_key) + 22);  /* "Authorization: Bearer " + key + \0 */
    if (!auth_header) {
        goto cleanup;
    }
    sprintf(auth_header, "Authorization: Bearer %s", ai_ctx->api_key);
    headers = curl_slist_append(headers, auth_header);
    free(auth_header);  /* curl_slist_append makes a copy */
    
    if (!headers) {
        goto cleanup;
    }
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!headers) {
        goto cleanup;
    }
    
    /* Set up curl options */
    curl_easy_setopt(curl, CURLOPT_URL, OPENAI_API_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    /* Perform request */
    res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        goto cleanup;
    }
    
    if (!response || !response[0]) {
        goto cleanup;
    }

    /* Parse JSON response */
    char *content = NULL;
    size_t content_size = 0;
    
    /* First find the choices array */
    char *choices_start = strstr(response, "\"choices\"");
    if (!choices_start) {
        goto cleanup;
    }
    
    /* Find the first message object */
    char *message_start = strstr(choices_start, "\"message\"");
    if (!message_start) {
        goto cleanup;
    }
    
    /* Find content within message */
    char *content_start = strstr(message_start, "\"content\"");
    if (!content_start) {
        goto cleanup;
    }
    
    /* Find the actual content value start */
    content_start = strchr(content_start + 9, '"');  /* Skip "content" and find opening quote */
    if (!content_start) {
        goto cleanup;
    }
    content_start++;  /* Skip the opening quote */
    
    char *content_end = content_start;
    int in_escape = 0;
    
    /* Find the actual end of content by handling escaped quotes */
    while (*content_end) {
        if (*content_end == '\\' && !in_escape) {
            in_escape = 1;
        } else {
            if (*content_end == '"' && !in_escape) {
                break;
            }
            in_escape = 0;
        }
        content_end++;
    }
    
    if (*content_end == '"') {
        content_size = content_end - content_start;
        content = malloc(content_size + 1);
        if (content) {
            /* Copy and unescape the content */
            char *w = content;
            const char *r = content_start;
            while (r < content_end) {
                if (*r == '\\' && *(r + 1)) {
                    r++;
                    switch (*r) {
                        case 'n': *w++ = '\n'; break;
                        case 'r': *w++ = '\r'; break;
                        case 't': *w++ = '\t'; break;
                        case '"': *w++ = '"';  break;
                        case '\\': *w++ = '\\'; break;
                        default:  *w++ = *r;   break;
                    }
                } else {
                    *w++ = *r;
                }
                r++;
            }
            *w = '\0';
            
            /* Store the response for analysis */
            ai_ctx->last_response = strdup(content);
            
            /* Only parse and execute commands if in ghost mode */
            if (ai_ctx->is_ghost_mode) {
                /* Parse and execute commands */
                size_t cmd_count;
                char **commands = ghost_ai_parse_commands(content, &cmd_count);
                
                if (commands && cmd_count > 0) {
                    ghost_ai_execute_commands(commands, cmd_count, shell_ctx);
                    
                    /* Clean up commands */
                    for (size_t i = 0; i < cmd_count; i++) {
                        if (commands[i]) {
                            free(commands[i]);
                        }
                    }
                    free(commands);
                    result = 0;  /* Success */
                }
            } else {
                result = 0;  /* Success, but don't execute commands */
            }
            
            free(content);
        }
    }
    
    /* Add AI response to history */
    if (ai_ctx->last_response) {
        ghost_ai_add_to_history(ai_ctx, MESSAGE_ASSISTANT, ai_ctx->last_response);
    }
    
cleanup:
    if (headers) curl_slist_free_all(headers);
    if (curl) curl_easy_cleanup(curl);
    free(escaped_prompt);
    free(payload);
    free(response);
    
    return result;
}

char **ghost_ai_parse_commands(const char *ai_response, size_t *cmd_count) {
    if (!ai_response || !cmd_count) {
        return NULL;
    }
    
    /* Make a copy of the response */
    char *response_copy = strdup(ai_response);
    if (!response_copy) {
        return NULL;
    }
    
    char **commands = NULL;
    *cmd_count = 0;
    
    /* Count non-empty lines */
    char *ptr = response_copy;
    char *line_start = ptr;
    while (ptr && *ptr) {
        if (*ptr == '\n' || *ptr == '\0') {
            /* Check if line is non-empty */
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
    
    /* Check last line if it doesn't end with newline */
    if (line_start && *line_start && strlen(line_start) > 0) {
        (*cmd_count)++;
    }
    
    if (*cmd_count == 0) {
        free(response_copy);
        return NULL;
    }
    
    /* Allocate command array */
    commands = calloc(*cmd_count + 1, sizeof(char*));
    if (!commands) {
        free(response_copy);
        return NULL;
    }
    
    /* Parse commands */
    char *saveptr = NULL;
    char *line = strtok_r(response_copy, "\n", &saveptr);
    size_t i = 0;
    
    while (line && i < *cmd_count) {
        /* Skip empty lines and trim whitespace */
        while (line && *line && isspace(*line)) line++;
        
        if (line && *line) {
            char *end = line + strlen(line) - 1;
            while (end > line && isspace(*end)) end--;
            *(end + 1) = '\0';
            
            if (strlen(line) > 0) {
                commands[i] = strdup(line);
                if (!commands[i]) {
                    /* Clean up */
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
    
    *cmd_count = i;  /* Update to actual count */
    commands[i] = NULL;  /* NULL terminate the array */
    
    free(response_copy);
    
    return commands;
}

void ghost_ai_display_command(const char *command, char *modified_command, size_t modified_size) {
    if (!command || !modified_command) return;
    
    size_t len = strlen(command);
    if (len == 0) return;

    /* Save terminal state */
    struct termios old_term, new_term;
    tcgetattr(STDIN_FILENO, &old_term);
    new_term = old_term;
    
    /* Set terminal to raw mode but keep some flags for proper display */
    new_term.c_lflag &= ~(ICANON | ECHO);
    new_term.c_cc[VMIN] = 1;
    new_term.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);

    /* Initialize modified command */
    strncpy(modified_command, command, modified_size - 1);
    modified_command[modified_size - 1] = '\0';
    size_t pos = strlen(modified_command);
    
    /* Clear current line and move cursor to beginning */
    printf("\r\033[K");
    
    static char prompt[1024];
    char *path = get_formatted_path();
    format_shell_prompt(prompt, sizeof(prompt), "ghost", path);
    printf("%s%s", prompt, modified_command);
    fflush(stdout);
    free(path);
    
    /* Set up timeout using microsecond precision */
    struct timeval start_tv, current_tv;
    gettimeofday(&start_tv, NULL);
    
    /* Edit loop - run for exactly 1.5 seconds */
    while (1) {
        gettimeofday(&current_tv, NULL);
        long elapsed_usec = (current_tv.tv_sec - start_tv.tv_sec) * 1000000L + 
                          (current_tv.tv_usec - start_tv.tv_usec);
        
        if (elapsed_usec >= 1500000) { /* 1.5 seconds in microseconds */
            break;
        }
        
        /* Calculate remaining time for select */
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000; /* 50ms timeout for responsive editing */
        
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
    
    /* Restore terminal state */
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    
    /* Print final newline */
    printf("\n");
    fflush(stdout);
}

char *ghost_ai_capture_command_output(const char *command, ghost_ai_context *ai_ctx) {
    FILE *fp;
    char *output = NULL;
    size_t total_size = 0;
    char buffer[4096];

    /* Open the command for reading */
    fp = popen(command, "r");
    if (fp == NULL) {
        return NULL;
    }

    /* Read the command output */
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

    /* Add command output to history */
    if (output && ai_ctx && ai_ctx->history) {
        ghost_ai_add_to_history(ai_ctx, MESSAGE_COMMAND_OUTPUT, output);
    }

    return output;
}

int ghost_ai_analyze_output(const char *original_prompt, const char *command_output, ghost_ai_context *ai_ctx, struct shell_context *shell_ctx) {
    char *analysis_prompt = NULL;
    size_t prompt_size;
    int result;

    /* Create analysis prompt */
    prompt_size = strlen(original_prompt) + strlen(command_output) + 200;
    analysis_prompt = malloc(prompt_size);
    if (!analysis_prompt) {
        return 1;
    }

    snprintf(analysis_prompt, prompt_size,
        "The user requested: '%s'\n"
        "The command output was:\n%s\n"
        "Please analyze if this output satisfies the user's request. "
        "If it is correct, respond with only 'SUCCESS' (this will not be shown to the user). "
        "If it is not correct, explain what should be done differently.",
        original_prompt, command_output);

    /* Store the current last_response */
    char *prev_response = ai_ctx->last_response;
    ai_ctx->last_response = NULL;

    /* Process the analysis request but don't show the response to the user */
    ai_ctx->is_ghost_mode = 0;  /* Temporarily disable response output */
    result = ghost_ai_process(analysis_prompt, ai_ctx, shell_ctx);
    ai_ctx->is_ghost_mode = 1;  /* Re-enable response output */
    
    /* If we got a response and it indicates the command wasn't successful */
    if (ai_ctx->last_response) {
        /* Check if the response indicates success */
        if (strstr(ai_ctx->last_response, "SUCCESS") != NULL) {
            /* Command was successful, no need for follow-up */
            result = 0;
        } else {
            /* Command wasn't successful, try alternative approach */
            result = ghost_ai_handle_followup(original_prompt, command_output, 
                                            ai_ctx->last_response, ai_ctx, shell_ctx);
        }
    }

    /* Restore the previous response */
    free(ai_ctx->last_response);
    ai_ctx->last_response = prev_response;
    
    free(analysis_prompt);
    return result;
}

int ghost_ai_handle_followup(const char *original_prompt, const char *command_output, const char *analysis_response, ghost_ai_context *ai_ctx, struct shell_context *shell_ctx) {
    /* Don't follow up if the response indicates success */
    if (strstr(analysis_response, "SUCCESS") != NULL) {
        return 0;
    }

    /* Check if the analysis response contains suggestions for different commands */
    if (strstr(analysis_response, "suggest") || strstr(analysis_response, "should") || 
        strstr(analysis_response, "could") || strstr(analysis_response, "would") ||
        strstr(analysis_response, "try") || strstr(analysis_response, "instead")) {
        
        /* Create a follow-up prompt */
        char *followup_prompt = malloc(strlen(original_prompt) + strlen(command_output) + 200);
        if (!followup_prompt) {
            return 1;
        }

        snprintf(followup_prompt, strlen(original_prompt) + strlen(command_output) + 200,
            "The previous command did not fully satisfy the request: '%s'\n"
            "Please provide the correct command(s) to achieve this goal.\n"
            "Provide ONLY the commands to run, no explanation.",
            original_prompt);

        /* Process the follow-up request */
        printf("\nTrying alternative approach...\n");
        int result = ghost_ai_process(followup_prompt, ai_ctx, shell_ctx);
        
        free(followup_prompt);
        return result;
    }
    
    return 0;  /* No follow-up needed */
}

void ghost_ai_execute_commands(char **commands, size_t cmd_count, struct shell_context *shell_ctx) {
    if (!commands || !shell_ctx || cmd_count == 0) {
        return;
    }

    for (size_t i = 0; i < cmd_count; i++) {
        char modified_command[4096];
        char *output;

        /* Display the command with typing effect */
        ghost_ai_display_command(commands[i], modified_command, sizeof(modified_command));
        
        /* Execute the command and capture output */
        output = ghost_ai_capture_command_output(modified_command, shell_ctx->ai_ctx);
        if (output) {
            /* Print the output */
            printf("%s", output);
            
            /* Analyze the output with AI and stop if successful */
            if (ghost_ai_analyze_output(shell_ctx->last_prompt, output, shell_ctx->ai_ctx, shell_ctx) == 0) {
                free(output);
                break;  /* Stop executing commands if we got a successful result */
            }
            
            free(output);
        }
    }
}
