#ifndef MACRO_H
#define MACRO_H

#include <assert.h>
#include <stdio.h>
#include <string.h>

/**
 * Assert macro
 */
#ifndef NDEBUG
#define ASSERT(cond) assert(cond)
#else 
    #define ASSERT(cond)
#endif  // NDEBUG

/**
 *  Logging macros
 */
#ifndef NDEBUG
#define LOG_DEBUG(...) \
    printf("D/LOG/%s(): ", __func__); \
    printf(__VA_ARGS__);\
    printf("\n");
#ifdef DEBUG_EXTRA
#define LOG_DEBUG_EXTRA(...) \
    printf("D/LOG/%s(%lu): ", __func__, crt.curr_coro_i); \
    printf(__VA_ARGS__);\
    printf("\n");
#endif  // LOG_DEBUG_EXTRA
#else 
    #define LOG_DEBUG(...)
    #define LOG_DEBUG_EXTRA(...)
#endif  // NDEBUG

#define LOG_FATAL(...) \
    dprintf(STDERR_FILENO, "F/LOG/%s(): ", __func__); \
    dprintf(STDERR_FILENO, __VA_ARGS__);              \
    dprintf(STDERR_FILENO, "\n");                     \
    abort();

#define LOG_ERROR(...) \
    if (errno != 0) {                                                                \
        dprintf(STDERR_FILENO, "E/LOG/%s() (%s): ", __func__, strerror(errno)); \
    } else {                                                                         \
        dprintf(STDERR_FILENO, "E/LOG/%s(): ", __func__);                       \
    }                                                                                \
    dprintf(STDERR_FILENO, __VA_ARGS__);                                             \
    dprintf(STDERR_FILENO, "\n");

#endif  // MACRO_H
