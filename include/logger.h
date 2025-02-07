#ifndef GHOST_SHELL_LOGGER_H
#define GHOST_SHELL_LOGGER_H

#include <stdio.h>

typedef enum {
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_ERROR = 1,
    LOG_LEVEL_INFO = 2,
    LOG_LEVEL_DEBUG = 3
} log_level_t;

// Initialize logging system with specified log level
int logger_init(void);

// Clean up logging system
void logger_cleanup(void);

// Set the current log level
void logger_set_level(log_level_t level);

// Get the current log level
log_level_t logger_get_level(void);

// Core logging functions
void logger_log_error(const char* file, int line, const char* fmt, ...);
void logger_log_info(const char* file, int line, const char* fmt, ...);
void logger_log_debug(const char* file, int line, const char* fmt, ...);

// Convenience macros
#define LOG_ERROR(...) logger_log_error(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) logger_log_info(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) logger_log_debug(__FILE__, __LINE__, __VA_ARGS__)

#endif // GHOST_SHELL_LOGGER_H
