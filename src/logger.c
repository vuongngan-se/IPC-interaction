#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

static FILE *log_file = NULL;

static const char *level_names[] = {
    "DEBUG", "INFO", "WARN", "ERROR"
};

/* ANSI color codes for terminal */
static const char *level_colors[] = {
    "\033[36m", /* Cyan for DEBUG */
    "\033[32m", /* Green for INFO */
    "\033[33m", /* Yellow for WARN */
    "\033[31m"  /* Red for ERROR */
};
static const char *color_reset = "\033[0m";

int logger_init(const char *log_filepath)
{
    if (log_filepath != NULL) {
        log_file = fopen(log_filepath, "a");
        if (!log_file) {
            perror("[Logger] Failed to open log file");
            return -1;
        }
    }
    return 0;
}

void logger_close(void)
{
    if (log_file != NULL) {
        fflush(log_file);
        fclose(log_file);
        log_file = NULL;
    }
}

void logger_log(LogLevel level, const char *file, int line, const char *fmt, ...)
{
    (void)file;
    (void)line;

    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);

    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);

    pid_t pid = getpid();

    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    /* Output to Terminal (with color) */
    printf("%s[%s] [PID:%5d] [%-5s]%s %s\n",
           level_colors[level],
           time_buf,
           (int)pid,
           level_names[level],
           color_reset,
           message);
    fflush(stdout);

    /* Output to File (without color) */
    if (log_file != NULL) {
        fprintf(log_file, "[%s] [PID:%5d] [%-5s] %s\n",
                time_buf,
                (int)pid,
                level_names[level],
                message);
        fflush(log_file);
    }
}
