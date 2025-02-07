#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>
#include <pwd.h>
#include "logger.h"

static FILE* log_file = NULL;
static char log_path[1024];
static log_level_t current_log_level = LOG_LEVEL_NONE;

// Internal function to write log message
static void write_log(const char* level, const char* file, int line, const char* fmt, va_list args) {
    if (!log_file) return;

    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_buffer[26];
    strftime(time_buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);

    // Print timestamp, level, file and line
    fprintf(log_file, "[%s] %s %s:%d - ", time_buffer, level, file, line);

    // Print the actual message
    vfprintf(log_file, fmt, args);

    // Add newline if not present
    if (fmt[strlen(fmt) - 1] != '\n') {
        fprintf(log_file, "\n");
    }

    fflush(log_file);
}

int logger_init(void) {
    struct passwd *pw = getpwuid(getuid());
    if (pw == NULL) {
        return -1;
    }

    snprintf(log_path, sizeof(log_path), "%s/.ghsh_log", pw->pw_dir);
    
    // Clear existing log file by opening in write mode
    log_file = fopen(log_path, "w");
    if (log_file == NULL) {
        return -1;
    }

    // Set initial log level based on build type
#ifdef DEBUG
    current_log_level = LOG_LEVEL_DEBUG;
#else
    current_log_level = LOG_LEVEL_INFO;
#endif

    time_t now = time(NULL);
    char* time_str = ctime(&now);
    fprintf(log_file, "=== Ghost Shell Log Started at %s===\n", time_str);
    fflush(log_file);
    
    return 0;
}

void logger_cleanup(void) {
    if (log_file) {
        time_t now = time(NULL);
        char* time_str = ctime(&now);
        fprintf(log_file, "=== Ghost Shell Log Ended at %s===\n", time_str);
        fclose(log_file);
        log_file = NULL;
    }
}

void logger_set_level(log_level_t level) {
    current_log_level = level;
}

log_level_t logger_get_level(void) {
    return current_log_level;
}

void logger_log_error(const char* file, int line, const char* fmt, ...) {
    if (current_log_level >= LOG_LEVEL_ERROR) {
        va_list args;
        va_start(args, fmt);
        write_log("ERROR", file, line, fmt, args);
        va_end(args);
    }
}

void logger_log_info(const char* file, int line, const char* fmt, ...) {
    if (current_log_level >= LOG_LEVEL_INFO) {
        va_list args;
        va_start(args, fmt);
        write_log("INFO", file, line, fmt, args);
        va_end(args);
    }
}

void logger_log_debug(const char* file, int line, const char* fmt, ...) {
    if (current_log_level >= LOG_LEVEL_DEBUG) {
        va_list args;
        va_start(args, fmt);
        write_log("DEBUG", file, line, fmt, args);
        va_end(args);
    }
}
