#ifndef ERROR_H
#define ERROR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define ERROR_MAX_FRAMES 16

typedef enum {
    ERR_OK = 0,
    ERR_OUT_OF_MEMORY,
    ERR_INVALID_ARGUMENT,
    ERR_IO,
    ERR_NOT_FOUND,
    ERR_TIMEOUT,
    ERR_INTERNAL
} ErrorCode;

typedef struct {
    const char *file;
    int line;
    const char *function;
    const char *context;
} ErrorFrame;

typedef struct {
    ErrorCode code;
    size_t depth;
    ErrorFrame frames[ERROR_MAX_FRAMES];
} Error;

static inline Error error_create(ErrorCode code, const char *file, int line, const char *func, const char *context) {
    Error err;
    err.code = code;
    err.depth = 1;
    err.frames[0].file = file;
    err.frames[0].line = line;
    err.frames[0].function = func;
    err.frames[0].context = context;
    return err;
}

static inline void error_push(Error *err, const char *file, int line, const char *func, const char *context) {
    if (err && err->depth < ERROR_MAX_FRAMES) {
        ErrorFrame *frame = &err->frames[err->depth++];
        frame->file = file;
        frame->line = line;
        frame->function = func;
        frame->context = context;
    }
}

static inline const char *error_code_str(ErrorCode code) {
    switch (code) {
        case ERR_OK: return "ERR_OK";
        case ERR_OUT_OF_MEMORY: return "ERR_OUT_OF_MEMORY";
        case ERR_INVALID_ARGUMENT: return "ERR_INVALID_ARGUMENT";
        case ERR_IO: return "ERR_IO";
        case ERR_NOT_FOUND: return "ERR_NOT_FOUND";
        case ERR_TIMEOUT: return "ERR_TIMEOUT";
        case ERR_INTERNAL: return "ERR_INTERNAL";
        default: return "ERR_UNKNOWN";
    }
}

static inline void error_print(const Error *err, FILE *stream) {
    if (!err || err->code == ERR_OK) return;

    FILE *out = stream ? stream : stderr;
    fprintf(out, "Error [%s (%d)]\n", error_code_str(err->code), err->code);
    fprintf(out, "Callstack breadcrumb trace (root cause first):\n");

    for (size_t i = 0; i < err->depth; ++i) {
        const ErrorFrame *f = &err->frames[i];
        fprintf(out, "  [%zu] %s:%d in %s()\n        Context: %s\n",
                i, f->file, f->line, f->function, f->context ? f->context : "");
    }
}

#define ERROR_CREATE(code, context) \
    error_create((code), __FILE__, __LINE__, __func__, (context))

#define ERROR_PUSH(err_ptr, context) \
    error_push((err_ptr), __FILE__, __LINE__, __func__, (context))

#define DEFINE_RESULT(Type, Name) \
    typedef struct Name##Result { \
        bool ok; \
        union { \
            Type value; \
            Error err; \
        }; \
    } Name##Result; \
    static inline Name##Result Name##_ok(Type val) { \
        Name##Result res; \
        res.ok = true; \
        res.value = val; \
        return res; \
    } \
    static inline Name##Result Name##_err(Error err) { \
        Name##Result res; \
        res.ok = false; \
        res.err = err; \
        return res; \
    }

DEFINE_RESULT(void *, Ptr)
DEFINE_RESULT(int, Int)
DEFINE_RESULT(size_t, Size)
DEFINE_RESULT(bool, Bool)

#define RESULT_OK(TypeResultName, val) \
    TypeResultName##_ok(val)

#define RESULT_ERR(TypeResultName, err) \
    TypeResultName##_err(err)

#define TRY_ASSIGN(ResultType, dest, expr, err_out, cleanup_label) \
    do { \
        ResultType _res = (expr); \
        if (!_res.ok) { \
            (err_out) = _res.err; \
            ERROR_PUSH(&(err_out), #expr); \
            goto cleanup_label; \
        } \
        (dest) = _res.value; \
    } while (0)

#define TRY_CHECK(expr, err_out, cleanup_label) \
    do { \
        BoolResult _res = (expr); \
        if (!_res.ok) { \
            (err_out) = _res.err; \
            ERROR_PUSH(&(err_out), #expr); \
            goto cleanup_label; \
        } \
    } while (0)

#define TRY_RETURN(ResultType, ReturnResultName, dest, expr) \
    do { \
        ResultType _res = (expr); \
        if (!_res.ok) { \
            Error _err = _res.err; \
            ERROR_PUSH(&_err, #expr); \
            return RESULT_ERR(ReturnResultName, _err); \
        } \
        (dest) = _res.value; \
    } while (0)

#endif
