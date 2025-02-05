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

GhostAIContext *ghost_ai_init(void) {
    GhostAIContext *ctx = NULL;
    const char *api_key = NULL;
    const char *system_prompt = NULL;
    
    /* Allocate context */
    ctx = calloc(1, sizeof(GhostAIContext));
    if (!ctx) {
        return NULL;
    }
    
    /* Initialize all fields to NULL */
    ctx->api_key = NULL;
    ctx->system_prompt = NULL;
    ctx->last_response = NULL;
    ctx->in_ghost_mode = 0;
    
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
        "If a task involes multiple steps, use shell redirection or piping to or other tools to combine them. "
        "If a task involves multiple commands combine them into one line. "
        "Use only standard Unix commands and tools that are likely to be available. ";
    
    ctx->system_prompt = strdup(system_prompt);
    if (!ctx->system_prompt) {
        ghost_ai_cleanup(ctx);
        return NULL;
    }
    
    return ctx;
}

void ghost_ai_cleanup(GhostAIContext *ai_ctx) {
    if (!ai_ctx) return;
    
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
    memset(ai_ctx, 0, sizeof(GhostAIContext));
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

int ghost_ai_process(const char *prompt, GhostAIContext *ai_ctx, struct ShellContext *shell_ctx) {
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
    
    /* Prepare JSON payload manually */
    int written = snprintf(payload, MAX_RESPONSE_SIZE,
             "{\"model\":\"%s\","
             "\"messages\":["
             "{\"role\":\"system\",\"content\":\"%s\"},"
             "{\"role\":\"user\",\"content\":\"%s\"}"
             "]}",
             OPENAI_MODEL, escaped_system, escaped_prompt);
    
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
            
            /* Parse and execute commands */
            int cmd_count;
            char **commands = ghost_ai_parse_commands(content, &cmd_count);
            
            if (commands && cmd_count > 0) {
                ghost_ai_execute_commands(commands, cmd_count, shell_ctx);
                
                /* Clean up commands */
                for (int i = 0; i < cmd_count; i++) {
                    if (commands[i]) {
                        free(commands[i]);
                    }
                }
                free(commands);
                result = 0;  /* Success */
            }
            
            free(content);
        }
    }
    
cleanup:
    if (headers) curl_slist_free_all(headers);
    if (curl) curl_easy_cleanup(curl);
    free(escaped_prompt);
    free(escaped_system);
    free(payload);
    free(response);
    
    return result;
}

char **ghost_ai_parse_commands(const char *ai_response, int *cmd_count) {
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
    int i = 0;
    
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
                    for (int j = 0; j < i; j++) {
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
    
    /* Display initial command */
    printf("%sghost%s@ghsh:~$ %s", COLOR_RED, COLOR_RESET, modified_command);
    fflush(stdout);
    
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
                        printf("\r\033[K%sghost%s@ghsh:~$ %s", 
                               COLOR_RED, COLOR_RESET, modified_command);
                        fflush(stdout);
                    }
                }
                else if (c >= 32 && c <= 126 && pos < modified_size - 1) {
                    modified_command[pos++] = c;
                    modified_command[pos] = '\0';
                    printf("\r\033[K%sghost%s@ghsh:~$ %s", 
                           COLOR_RED, COLOR_RESET, modified_command);
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

void ghost_ai_execute_commands(char **commands, int cmd_count, struct ShellContext *shell_ctx) {
    if (!commands || !shell_ctx || cmd_count <= 0) {
        return;
    }
    
    Command *cmd = NULL;
    char modified_command[4096];
    
    for (int i = 0; i < cmd_count && commands[i]; i++) {
        if (!commands[i]) continue;
        
        /* Display command with prompt and allow editing */
        ghost_ai_display_command(commands[i], modified_command, sizeof(modified_command));
        
        /* Add modified command to history */
        add_history(modified_command);
        if (hist && shell_ctx->history_file) {
            history(hist, &ev, H_ENTER, modified_command);
            history(hist, &ev, H_SAVE, shell_ctx->history_file);
        }
        
        /* Parse command */
        cmd = parse_command(modified_command);
        if (!cmd) {
            continue;
        }
        
        /* Execute command */
        if (cmd->name) {
            shell_ctx->last_status = execute_command(cmd, shell_ctx);
        }
        
        /* Clean up */
        free_command(cmd);
        cmd = NULL;
        
        /* Add a newline after command output if there are more commands */
        if (i < cmd_count - 1) {
            printf("\n");
        }
    }
}
