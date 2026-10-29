#ifndef LOG_H
#define LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

typedef enum {
    LOG_LEVEL_TRACE = 0,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL,
    LOG_LEVEL_COUNT
} LogLevel;

typedef struct {
    uint64_t timestamp_ms;
    LogLevel level;
    const char *file;
    int line;
    const char *function;
    const char *message;
} LogRecord;

typedef void (*LogSinkFn)(const LogRecord *record, void *user_data);

#ifndef LOG_MIN_LEVEL
#define LOG_MIN_LEVEL LOG_LEVEL_TRACE
#endif

void log_init(void);
void log_set_level(LogLevel level);
LogLevel log_get_level(void);
void log_add_sink(LogSinkFn fn, void *user_data, LogLevel min_level);
void log_set_quiet(bool quiet);

void log_sink_console(const LogRecord *record, void *user_data);
void log_sink_file(const LogRecord *record, void *user_data);
void log_sink_ring_buffer(const LogRecord *record, void *user_data);
void log_dump_ring_buffer(FILE *stream);

void log_emit(LogLevel level, const char *file, int line, const char *func, const char *fmt, ...);
void log_emitv(LogLevel level, const char *file, int line, const char *func, const char *fmt, va_list args);

#if LOG_MIN_LEVEL <= 0
#define LOG_TRACE(...) log_emit(LOG_LEVEL_TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__)
#else
#define LOG_TRACE(...) ((void)0)
#endif

#if LOG_MIN_LEVEL <= 1
#define LOG_DEBUG(...) log_emit(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#else
#define LOG_DEBUG(...) ((void)0)
#endif

#if LOG_MIN_LEVEL <= 2
#define LOG_INFO(...) log_emit(LOG_LEVEL_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)
#else
#define LOG_INFO(...) ((void)0)
#endif

#if LOG_MIN_LEVEL <= 3
#define LOG_WARN(...) log_emit(LOG_LEVEL_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)
#else
#define LOG_WARN(...) ((void)0)
#endif

#if LOG_MIN_LEVEL <= 4
#define LOG_ERROR(...) log_emit(LOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#else
#define LOG_ERROR(...) ((void)0)
#endif

#if LOG_MIN_LEVEL <= 5
#define LOG_FATAL(...) log_emit(LOG_LEVEL_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)
#else
#define LOG_FATAL(...) ((void)0)
#endif

#endif
