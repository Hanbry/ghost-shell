/* json_parser.c */
#include "json_parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

char *parse_ai_response_content(const char *json_response) {
    if (!json_response) return NULL;

    const char *choices_start = strstr(json_response, "\"choices\"");
    if (!choices_start) {
        fprintf(stderr, "No choices in response\n");
        return NULL;
    }

    const char *message_start = strstr(choices_start, "\"message\"");
    if (!message_start) {
        fprintf(stderr, "No message in choices\n");
        return NULL;
    }

    const char *content_field = strstr(message_start, "\"content\"");
    if (!content_field) {
        fprintf(stderr, "No content field in message\n");
        return NULL;
    }

    /* Skip past "content" and the following characters until the opening quote */
    const char *content_start = strchr(content_field + 9, '"');
    if (!content_start) {
        fprintf(stderr, "Malformed content field\n");
        return NULL;
    }
    content_start++; // skip opening quote

    const char *ptr = content_start;
    int in_escape = 0;
    while (*ptr) {
        if (*ptr == '\\' && !in_escape) {
            in_escape = 1;
        } else {
            if (*ptr == '"' && !in_escape) {
                break;
            }
            in_escape = 0;
        }
        ptr++;
    }

    if (*ptr != '"') {
        fprintf(stderr, "Content field not properly terminated\n");
        return NULL;
    }

    size_t content_size = ptr - content_start;
    char *content = malloc(content_size + 1);
    if (!content) return NULL;

    char *w = content;
    const char *r = content_start;
    while (r < ptr) {
        if (*r == '\\' && (r + 1) && (*(r + 1) != '\0')) {
            r++;
            switch (*r) {
                case 'n': *w++ = '\n'; break;
                case 'r': *w++ = '\r'; break;
                case 't': *w++ = '\t'; break;
                case '"': *w++ = '"'; break;
                case '\\': *w++ = '\\'; break;
                default:  *w++ = *r; break;
            }
        } else {
            *w++ = *r;
        }
        r++;
    }
    *w = '\0';

    return content;
}
