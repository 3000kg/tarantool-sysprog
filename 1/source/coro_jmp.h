#include <setjmp.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

#ifndef CORO_COMMON_DATA
#error "You are expected to specify what you want to share between coroutines "\
       "via CORO_COMMON_DATA"
#endif  // CORO_COMMON_DATA
#ifndef CORO_LOCAL_DATA
#error "You are expected to specify what you want to store in a coroutine "\
       "via CORO_LOCAL_DATA"
#endif  // CORO_LOCAL_DATA
#ifndef CORO_LOCAL_STACK_FRAME
#error "You are expected to specify a coroutine stack frame type "\
       "via CORO_LOCAL_STACK_FRAME"
#endif  // CORO_LOCAL_STACK_FRAME

/**
 * Coroutines library. It allows to split execution of a task
 * into a set of coroutines. Possible example of usage:
 *
 *
 * foreach (coro : coros)
 *     coro_init(coro);
 * coro_call(func_to_split);
 *
 *
 * void other_func1()
 * {
 *     ...
 *     coro_return();
 * }
 *
 * void other_func2()
 * {
 *     coro_call(func1);
 *     ...
 *     coro_return();
 * }
 *
 * void func_to_split()
 * {
 *     ...
 *     coro_call(other_func1);
 *     ...
 *     coro_yield();
 *     ...
 *     coro_call(other_func2);
 *     ...
 *     coro_finish();
 *     coro_wait_all();
 * }
 */

/**
 * This struct describes one single coroutine. It stores its
 * local variables and a point where it sohuld return.
 */
struct coro_stack_frame {
    /**
     * Stack of points remembered before a call of a function
     * from the coroutine. Before each new call stack position
     * is remembered here, and the function returns here via
     * longjmp.
     */
    jmp_buf ret_point;

    CORO_LOCAL_STACK_FRAME uframe;
};
struct coro {
    /**
     * Important thing - execution position where that
     * coroutine stopped last time.
     */
    jmp_buf exec_point;

    /**
     * This flag is set when the coroutine has finished its
     * task. It is used to wait until all the coroutines are
     * finished.
     */
    bool is_finished;

    /**
     * Intended for checks outside of the coroutine that it was
     * finished with no errors.
     */
    bool no_errors_occurred;

    struct {
        clock_t clocks_spent;
        clock_t timestamp;
    };
    
    CORO_LOCAL_DATA;

    /** Coroutine local stack */
    struct {
        size_t stack_pointer;
        size_t stack_capacity;
        struct coro_stack_frame *stack;
    };
};

/**
 * In your code it should be dynamic, not const. Here it is 3 for
 * simplicity.
 */
static struct CoRuntime
{
    size_t coro_count;
    size_t curr_coro_i;
    struct coro *coros;
    CORO_COMMON_DATA;
} crt;

/**
 * Index of the currently working coroutine. It is used to learn
 * which coroutine should be scheduled next, and to get your
 * current local variables.
 */


/** Get currently working coroutine. */
#define coro_this() (&crt.coros[crt.curr_coro_i])

/** Declare that this curoutine has finished. */
#define coro_finish() ({ \
    if (coro_this()->stack_pointer != 0) {                          \
        LOG_ERROR("coro[%lu] stack is corrupted, sp = %lu",         \
                  crt.curr_coro_i, coro_this()->stack_pointer);     \
        coro_this()->no_errors_occurred = false;                    \
    }                                                               \
    free(coro_this()->stack);                                       \
    coro_this()->stack = NULL;                                      \
    coro_this()->is_finished = true;                                \
    coro_this()->clocks_spent += clock() - coro_this()->timestamp;  \
    coro_this()->timestamp = clock();                               \
})

/**
 * This macro stops the current coroutine and switches to another
 * one. Check is not in a function, because setjmp result can not
 * be used after 'return'. You should keep it as macros. In your
 * code instead of real call and 'return' use coro_call() and
 * coro_return().
 */
#define coro_yield() ({ \
    int old_i = crt.curr_coro_i;                                                \
    crt.curr_coro_i = (crt.curr_coro_i + 1) % crt.coro_count;                   \
    if (setjmp(crt.coros[old_i].exec_point) == 0) {                             \
        ASSERT(crt.coros[old_i].timestamp != -1);                               \
        crt.coros[old_i].clocks_spent += clock() - crt.coros[old_i].timestamp;  \
        crt.coros[crt.curr_coro_i].timestamp = clock();                         \
        longjmp(crt.coros[crt.curr_coro_i].exec_point, 1);                      \
    }                                                                           \
})

/** Initialize a coroutine. */
#define coro_init(coro_idx) ({ \
    ASSERT(crt.coros != NULL);                      \
    crt.coros[coro_idx].is_finished = false;        \
    crt.coros[coro_idx].no_errors_occurred = true;  \
                                                    \
    crt.coros[coro_idx].stack_pointer = 0;          \
    crt.coros[coro_idx].stack_capacity = 0;         \
    crt.coros[coro_idx].stack = NULL;               \
                                                    \
    crt.coros[coro_idx].clocks_spent = 0;           \
    crt.coros[coro_idx].timestamp = -1;             \
                                                    \
    setjmp(crt.coros[coro_idx].exec_point);         \
})

/**
 * Call a function, but do it safely, creating a point to jump
 * back from that function, instead of 'return'.
 */
#define coro_call(func, ...) ({ \
    struct coro *c = coro_this();                                                           \
    if ((c->stack_pointer + 1) >= c->stack_capacity) {                                      \
        size_t new_cap = (c->stack_pointer + 1) * 2;                                        \
        LOG_DEBUG("reallocating stack to (%lu)", new_cap);                                  \
        size_t new_size = new_cap * sizeof(struct coro_stack_frame);                        \
        c->stack =                                                                          \
            (struct coro_stack_frame*) realloc(c->stack, new_size);                         \
        if (c->stack == NULL) {                                                             \
            LOG_FATAL("unable to realloc(%lu) stack", new_size);                            \
        }                                                                                   \
        c->stack_capacity = new_cap;                                                        \
    }                                                                                       \
    LOG_DEBUG("creating return point");                                                     \
    if (setjmp(c->stack[c->stack_pointer + 1].ret_point) == 0) {                            \
        /* Pass arguments  via stack frame: */                                              \
        c->stack[c->stack_pointer + 1].uframe = (CORO_LOCAL_STACK_FRAME) {__VA_ARGS__};     \
        (c->stack_pointer)++;                                                               \
        LOG_DEBUG("stack frame arguments: `%s(%lu, %lu)`", #func,                           \
                  c->stack[c->stack_pointer].uframe.sort_from,                              \
                  c->stack[c->stack_pointer].uframe.sort_to);                               \
        func();                                                                             \
    }                                                                                       \
})

/**
 * Return from a function, previously called via coro_call().
 * Tricky thing - it does not use 'return', because it taints
 * all the jmp_buf's. Instead, it jumps out of that function.
 */
#define coro_return() ({ \
    struct coro *c = coro_this();                           \
    ASSERT(c->stack_pointer > 0);                           \
    ASSERT(c->stack_pointer < c->stack_capacity);           \
    longjmp(c->stack[(c->stack_pointer)--].ret_point, 1);   \
})

/** Wait until all the coroutines have finished. */
#define coro_wait_all() do { \
    bool is_all_finished = true;                        \
    for (size_t i = 0; i < crt.coro_count; ++i) {       \
        if (! crt.coros[i].is_finished) {               \
            fprintf(stderr, "Coro %lu: still active,"   \
                " re-scheduling\n", i);                 \
            is_all_finished = false;                    \
            break;                                      \
        }                                               \
    }                                                   \
    if (is_all_finished) {                              \
        fprintf(stderr, "No more active coros to "      \
            "schedule.\n");                             \
        break;                                          \
    }                                                   \
    coro_yield();                                       \
} while (true)
