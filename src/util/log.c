#include "log.h"
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>

typedef pthread_mutex_t LogLock;
#define LOG_LOCK_INIT(l)   pthread_mutex_init(l, NULL)
#define LOG_LOCK_ENTER(l)  pthread_mutex_lock(l)
#define LOG_LOCK_LEAVE(l)  pthread_mutex_unlock(l)

#define LOG_MAX_SINKS 8
#define LOG_RING_BUFFER_CAPACITY 64
#define LOG_MESSAGE_BUFFER_SIZE 1024
#define LOG_RING_MSG_LEN 256

typedef struct {
    LogSinkFn fn;
    void *user_data;
    LogLevel min_level;
} LogSinkEntry;

typedef struct {
    uint64_t timestamp_ms;
    LogLevel level;
    int line;
    char file[64];
    char function[48];
    char message[LOG_RING_MSG_LEN];
} RingRecord;

typedef struct {
    LogLevel current_level;
    bool quiet_console;
    LogLock lock;
    LogSinkEntry sinks[LOG_MAX_SINKS];
    size_t sink_count;

    RingRecord ring_buffer[LOG_RING_BUFFER_CAPACITY];
    size_t ring_head;
    size_t ring_count;
} LogContext;

static LogContext g_log_ctx = {
    .current_level = LOG_LEVEL_INFO,
    .quiet_console = false,
    .sink_count = 0,
    .ring_head = 0,
    .ring_count = 0
};

static uint64_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static const char *level_strings[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static const char *level_colors[] = {
    "\x1b[90m",
    "\x1b[36m",
    "\x1b[32m",
    "\x1b[33m",
    "\x1b[31m",
    "\x1b[35m"
};

void log_init(void) {
    static bool initialized = false;
    if (!initialized) {
        LOG_LOCK_INIT(&g_log_ctx.lock);
        log_add_sink(log_sink_console, NULL, LOG_LEVEL_TRACE);
        log_add_sink(log_sink_ring_buffer, NULL, LOG_LEVEL_TRACE);
        initialized = true;
    }
}

void log_set_level(LogLevel level) {
    g_log_ctx.current_level = level;
}

LogLevel log_get_level(void) {
    return g_log_ctx.current_level;
}

void log_set_quiet(bool quiet) {
    g_log_ctx.quiet_console = quiet;
}

void log_add_sink(LogSinkFn fn, void *user_data, LogLevel min_level) {
    LOG_LOCK_ENTER(&g_log_ctx.lock);
    if (g_log_ctx.sink_count < LOG_MAX_SINKS) {
        g_log_ctx.sinks[g_log_ctx.sink_count++] = (LogSinkEntry){
            .fn = fn,
            .user_data = user_data,
            .min_level = min_level
        };
    }
    LOG_LOCK_LEAVE(&g_log_ctx.lock);
}

void log_sink_console(const LogRecord *record, void *user_data) {
    (void)user_data;
    if (g_log_ctx.quiet_console) return;

    FILE *stream = (record->level >= LOG_LEVEL_WARN) ? stderr : stdout;
    const char *color = level_colors[record->level];
    const char *level_str = level_strings[record->level];

    time_t sec = (time_t)(record->timestamp_ms / 1000ULL);
    struct tm tm_info;
    localtime_r(&sec, &tm_info);

    char time_buf[24];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);

    fprintf(stream, "%s %s%-5s\x1b[0m \x1b[90m%s:%d:\x1b[0m %s\n",
            time_buf, color, level_str, record->file, record->line, record->message);
    fflush(stream);
}

void log_sink_file(const LogRecord *record, void *user_data) {
    FILE *f = (FILE *)user_data;
    if (!f) return;

    time_t sec = (time_t)(record->timestamp_ms / 1000ULL);
    struct tm tm_info;
    localtime_r(&sec, &tm_info);

    char time_buf[24];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);

    fprintf(f, "%s %-5s %s:%d in %s(): %s\n",
            time_buf, level_strings[record->level], record->file, record->line,
            record->function, record->message);
    fflush(f);
}

void log_sink_ring_buffer(const LogRecord *record, void *user_data) {
    (void)user_data;
    RingRecord *entry = &g_log_ctx.ring_buffer[g_log_ctx.ring_head];
    entry->timestamp_ms = record->timestamp_ms;
    entry->level = record->level;
    entry->line = record->line;

    strncpy(entry->file, record->file, sizeof(entry->file) - 1);
    entry->file[sizeof(entry->file) - 1] = '\0';

    strncpy(entry->function, record->function, sizeof(entry->function) - 1);
    entry->function[sizeof(entry->function) - 1] = '\0';

    strncpy(entry->message, record->message, sizeof(entry->message) - 1);
    entry->message[sizeof(entry->message) - 1] = '\0';

    g_log_ctx.ring_head = (g_log_ctx.ring_head + 1) % LOG_RING_BUFFER_CAPACITY;
    if (g_log_ctx.ring_count < LOG_RING_BUFFER_CAPACITY) {
        g_log_ctx.ring_count++;
    }
}

void log_dump_ring_buffer(FILE *stream) {
    FILE *out = stream ? stream : stderr;
    LOG_LOCK_ENTER(&g_log_ctx.lock);

    fprintf(out, "\n=== IN-MEMORY DIAGNOSTIC CRASH DUMP ===\n");
    if (g_log_ctx.ring_count == 0) {
        fprintf(out, "(No recent events recorded in ring buffer)\n");
        LOG_LOCK_LEAVE(&g_log_ctx.lock);
        return;
    }

    size_t start = (g_log_ctx.ring_count == LOG_RING_BUFFER_CAPACITY)
        ? g_log_ctx.ring_head
        : 0;

    for (size_t i = 0; i < g_log_ctx.ring_count; ++i) {
        size_t idx = (start + i) % LOG_RING_BUFFER_CAPACITY;
        RingRecord *r = &g_log_ctx.ring_buffer[idx];
        fprintf(out, "[%llu ms] [%-5s] %s:%d (%s): %s\n",
                (unsigned long long)r->timestamp_ms,
                level_strings[r->level],
                r->file, r->line, r->function, r->message);
    }
    fprintf(out, "=======================================\n\n");
    fflush(out);

    LOG_LOCK_LEAVE(&g_log_ctx.lock);
}

void log_emitv(LogLevel level, const char *file, int line, const char *func, const char *fmt, va_list args) {
    if (level < g_log_ctx.current_level) return;

    char msg_buf[LOG_MESSAGE_BUFFER_SIZE];
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);

    LogRecord record = {
        .timestamp_ms = get_time_ms(),
        .level = level,
        .file = file,
        .line = line,
        .function = func,
        .message = msg_buf
    };

    LOG_LOCK_ENTER(&g_log_ctx.lock);
    for (size_t i = 0; i < g_log_ctx.sink_count; ++i) {
        LogSinkEntry *sink = &g_log_ctx.sinks[i];
        if (level >= sink->min_level && sink->fn) {
            sink->fn(&record, sink->user_data);
        }
    }
    LOG_LOCK_LEAVE(&g_log_ctx.lock);

    if (level == LOG_LEVEL_FATAL) {
        log_dump_ring_buffer(stderr);
    }
}

void log_emit(LogLevel level, const char *file, int line, const char *func, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_emitv(level, file, line, func, fmt, args);
    va_end(args);
}
