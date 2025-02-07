#ifndef JSON_PARSER_H
#define JSON_PARSER_H

/* Parses the AI JSON response and returns the extracted content string.
 * The returned string is dynamically allocated and must be freed by the caller.
 * Returns NULL on error or if the content field cannot be found.
 */
char *parse_ai_response_content(const char *json_response);

#endif // JSON_PARSER_H
