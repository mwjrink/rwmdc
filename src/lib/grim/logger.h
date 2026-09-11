#pragma once

#include <lib/grim/bp.h>
#include <stdarg.h>
#include <stdio.h>

// Scopes are retained for call-site clarity; no runtime filtering on the hot path.
typedef enum LogScope {
    SCOPE_STARTUP, SCOPE_SHUTDOWN, SCOPE_WAYLAND, SCOPE_MATH, SCOPE_LOAD,
    SCOPE_MEM_ARENA, SCOPE_GFX_DEBUG, SCOPE_GFX_INIT, SCOPE_GFX_SWAPCHAIN,
    SCOPE_GFX_VALIDATION, SCOPE_GFX_BUFFER, SCOPE_GFX_PIPELINE,
    SCOPE_GFX_COMMAND_BUFFER, SCOPE_GFX_COMMANDS, SCOPE_GFX_DESCRIPTORS,
    SCOPE_GFX_IMAGE, SCOPE_GFX_EXT, SCOPE_GFX_PRESENT, SCOPE_GFX_SHADER,
    SCOPE_GFX_COMMAND_QUEUE
} LogScope;

__attribute__((format(printf, 5, 6)))
static void grim_log(const char *level, const char *scope, const char *file,
                     int line, const char *format, ...) {
    fprintf(stderr, "[%s %s] %s:%d: ", level, scope, file, line);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

#define CRITICAL_LOG(scope, ...) grim_log("critical", #scope, __FILE__, __LINE__, __VA_ARGS__)
#define ERROR_LOG(scope, ...) grim_log("error", #scope, __FILE__, __LINE__, __VA_ARGS__)
#ifdef LOG_LEVEL_WARNING
#define WARNING_LOG(scope, ...) grim_log("warning", #scope, __FILE__, __LINE__, __VA_ARGS__)
#else
#define WARNING_LOG(scope, ...) ((void)0)
#endif
#ifdef LOG_LEVEL_DEBUG
#define DEBUG_LOG(scope, ...) grim_log("debug", #scope, __FILE__, __LINE__, __VA_ARGS__)
#else
#define DEBUG_LOG(scope, ...) ((void)0)
#endif
#ifdef LOG_LEVEL_INFO
#define INFO_LOG(scope, ...) grim_log("info", #scope, __FILE__, __LINE__, __VA_ARGS__)
#else
#define INFO_LOG(scope, ...) ((void)0)
#endif
#ifdef LOG_LEVEL_VERBOSE
#define VERBOSE_LOG(scope, ...) grim_log("verbose", #scope, __FILE__, __LINE__, __VA_ARGS__)
#else
#define VERBOSE_LOG(scope, ...) ((void)0)
#endif
