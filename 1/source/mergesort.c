#define NDEBUG
#define DEBUG_EXTRA
#include <aio.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "macro.h"

typedef long int num_t;
const char* const O_FILE_NAME = "mergesorted.txt";
enum
{
    NUMBERS_PER_FILE_DEFAULT = 1000
};


#define CORO_LOCAL_DATA struct \
{                               \
    /* File representation */   \
    struct aiocb aio_control;   \
                                \
    /* File parsing */          \
    num_t *numbers;             \
    num_t temp_number;          \
    size_t numbers_size;        \
    size_t numbers_capacity;    \
    char *end_ptr;              \
    char *start_ptr;            \
                                \
    num_t target;               \
    size_t lower_idx;           \
    size_t upper_idx;           \
                                \
    /* Misc */                  \
    size_t cur_pos;             \
}

#define CORO_COMMON_DATA struct \
{                               \
    size_t total_n_numbers;     \
}


struct sframe_t
{
    size_t sort_from;
    size_t sort_to;
    size_t sep_idx;
};
#define CORO_LOCAL_STACK_FRAME struct sframe_t
#include "coro_jmp.h"

bool InitRuntime(int argc, char *argv[]);
bool AllocateCoroutines(int argc);
bool OpenFiles(char *argv[]);
bool AsyncReadFiles();

void CoroExec();
void ParseFile();
void QuickSort();
void SortRange(/* size_t sort_from, size_t sort_to */);
void AtomicSwap(num_t *x, num_t *y);

bool MergeFiles();
num_t GetMinimalNumber(size_t *coro_idx);

bool Free();

void PrintStatistics(clock_t timestamp);

int main(int argc, char* argv[])
{
    clock_t timestamp = clock();
    if (!InitRuntime(argc, argv)) {
        LOG_FATAL("failed to initialize runtime");
    }

    for (size_t i = 0; i < crt.coro_count; ++i) {
        if (coro_init(i) != 0) {
            break;
        }
    }
    coro_this()->timestamp = clock();
    coro_call(CoroExec);
    coro_finish();
    coro_wait_all();

    if (!MergeFiles()) {
        return 1;
    }
    PrintStatistics(timestamp);

    return Free() ? 0 : 1;
}

bool InitRuntime(int argc, char *argv[])
{
    if (!AllocateCoroutines(argc)) {
        return false;
    }
    if (!OpenFiles(argv)) {
        return false;
    }
    if (!AsyncReadFiles()) {
        return false;
    }
    return true;
}

bool AllocateCoroutines(int argc)
{
    if (argc <= 1) {
        LOG_ERROR("no input files provided");
        return false;
    }
    crt.coro_count = argc - 1;
    crt.coros = (struct coro*) calloc(crt.coro_count, sizeof(struct coro));
    if (crt.coros == NULL) {
        LOG_ERROR("calloc(%lu) failed", crt.coro_count);
        return false;
    }
    for (size_t i = 0; i < crt.coro_count; i++) {
        crt.coros[i].numbers = (num_t*) calloc((size_t) NUMBERS_PER_FILE_DEFAULT, sizeof(num_t));
        if (crt.coros[i].numbers == NULL) {
            LOG_ERROR("calloc(%lu) failed", (size_t) NUMBERS_PER_FILE_DEFAULT);
            return false;
        }
        crt.coros[i].numbers_capacity = (size_t) NUMBERS_PER_FILE_DEFAULT;
        crt.coros[i].numbers_size = 0;
    }
    return true;
}

bool OpenFiles(char *argv[])
{
    crt.total_n_numbers = 0;
    ASSERT(crt.coros != NULL);
    char** filenames = argv + 1;

    for (size_t i = 0; i < crt.coro_count; i++) {
        memset(&crt.coros[i].aio_control, 0, sizeof(struct aiocb));
        // Open a file:
        crt.coros[i].aio_control.aio_fildes = open(filenames[i], O_RDONLY);
        if (crt.coros[i].aio_control.aio_fildes == -1) {
            LOG_ERROR("Unable to open a file: \"%s\"", filenames[i]);
            return false;
        }

        // Calculate file size:
        off_t old_pos = lseek(crt.coros[i].aio_control.aio_fildes, 0, SEEK_CUR);
        off_t eof_pos = lseek(crt.coros[i].aio_control.aio_fildes, 0, SEEK_END);
        if (eof_pos == (off_t) -1) {
            LOG_ERROR("Can't get file size");
            return false;
        }
        crt.coros[i].aio_control.aio_nbytes = (size_t) eof_pos;
        ASSERT(old_pos != (off_t) -1);
        lseek(crt.coros[i].aio_control.aio_fildes, old_pos, SEEK_SET);

        // Allocate (size + 1) bytes because of EOF ('\0'):
        crt.coros[i].aio_control.aio_buf = (char*) calloc(crt.coros[i].aio_control.aio_nbytes + 1,
                                                          sizeof(char));
        if (crt.coros[i].aio_control.aio_buf  == NULL) {
            LOG_ERROR("calloc(%lu) failed", crt.coros[i].aio_control.aio_nbytes + 1);
            return false;
        }
        LOG_DEBUG("Opened a file (name = \"%s\", size = %lu)", filenames[i], crt.coros[i].aio_control.aio_nbytes);
    }
    return true;
}

bool AsyncReadFiles()
{
    ASSERT(crt.coros != NULL);
    for (size_t i = 0; i < crt.coro_count; i++) {
        aio_read(&crt.coros[i].aio_control);
    }
    return true;
}

void CoroExec()
{
    while (aio_error(&coro_this()->aio_control) == EINPROGRESS) {
        LOG_DEBUG("read-request[%lu] is in progress", crt.curr_coro_i);
        coro_yield();
    }
    if (close(coro_this()->aio_control.aio_fildes) != 0) {
        LOG_ERROR("Unable to close file[%lu]", crt.curr_coro_i);
        coro_this()->no_errors_occurred = false;
        coro_return();
    }
    coro_this()->aio_control.aio_fildes = -1;
    if (aio_return(&coro_this()->aio_control) != (ssize_t) coro_this()->aio_control.aio_nbytes) {
        LOG_ERROR("unable to read file[%lu]", crt.curr_coro_i);
        coro_this()->no_errors_occurred = false;
        coro_return();
    }
    LOG_DEBUG("AIO-read a file[%lu]", crt.curr_coro_i);

    coro_call(ParseFile);
    coro_yield();

    if (coro_this()->no_errors_occurred) {
        coro_call(QuickSort);
    } else {
        LOG_ERROR("Unable to parse file (idx = %lu)", crt.curr_coro_i);
    }
    coro_yield();

#ifndef NDEBUG
    LOG_DEBUG_EXTRA("Sorted file (%lu):", crt.curr_coro_i);
    for (size_t i = 0; i < coro_this()->numbers_size; i++) {
        LOG_DEBUG_EXTRA("%ld ", coro_this()->numbers[i]);
    }
#endif  // NDEBUG

    coro_yield();
    coro_return();
}

void ParseFile()
{
    coro_this()->end_ptr = NULL;                                                                    coro_yield();
    coro_this()->start_ptr = (char*) coro_this()->aio_control.aio_buf;                              coro_yield();
    coro_this()->numbers_size = 0;                                                                  coro_yield();
    while (*coro_this()->start_ptr != '\0') {
        if (coro_this()->numbers_size >= coro_this()->numbers_capacity) {
            LOG_DEBUG("reallocating from %lu to %lu", coro_this()->numbers_capacity,
                                                      (coro_this()->numbers_capacity + 1) * 2);
            coro_this()->numbers_capacity = (coro_this()->numbers_capacity + 1) * 2;                coro_yield();
            coro_this()->numbers = reallocarray(coro_this()->numbers, coro_this()->numbers_capacity,
                                                sizeof(num_t));                                     coro_yield();
            if (coro_this()->numbers == NULL) {
                LOG_ERROR("realloc(%lu) failed", coro_this()->numbers_capacity * sizeof(num_t));    coro_yield();
                coro_this()->no_errors_occurred = false;                                            coro_yield();
                coro_return();
            }
        }
        ASSERT(errno == 0);
        coro_this()->numbers[coro_this()->numbers_size] = strtol(coro_this()->start_ptr,
                                                                 &coro_this()->end_ptr, 0);         coro_yield();
        coro_this()->numbers_size++;                                                                coro_yield();
        if (errno != 0) {
            LOG_ERROR("strtoll() failed");                                                          coro_yield();
            coro_this()->no_errors_occurred = false;                                                coro_yield();
            coro_return();
        }
        if (coro_this()->start_ptr == coro_this()->end_ptr) {
            while (isspace(*coro_this()->start_ptr)) {                                              coro_yield();
                coro_this()->start_ptr++;
            }
            if (*coro_this()->start_ptr == '\0') {                                                  coro_yield();
                break;
            }
            LOG_ERROR("Unknown symbol: '%c'", *coro_this()->start_ptr);                             coro_yield();
            coro_this()->no_errors_occurred = false;                                                coro_yield();
            coro_return();
        }
        coro_this()->start_ptr = coro_this()->end_ptr;                                              coro_yield();
    }
    crt.total_n_numbers += coro_this()->numbers_size;                                               coro_yield();
    LOG_DEBUG("Parsed file (idx = %lu, n_numbers = %lu)", crt.curr_coro_i,
              coro_this()->numbers_size);                                                           coro_yield();
    coro_this()->no_errors_occurred = (*coro_this()->start_ptr == '\0') ? true : false;             coro_yield();
    coro_return();
}

void QuickSort()
{
    if (coro_this()->numbers_size > 1) {
        coro_call(SortRange, 0, coro_this()->numbers_size - 1);
    }
    coro_return();
}

void SortRange(/* size_t sort_from, size_t sort_to */)
{
#define SFRAME coro_this()->stack[coro_this()->stack_pointer].uframe
    LOG_DEBUG_EXTRA("SortRange %lu:%lu", SFRAME.sort_from, SFRAME.sort_to);
    ASSERT(SFRAME.sort_from < SFRAME.sort_to);
    coro_this()->lower_idx = SFRAME.sort_from;                                              
    coro_this()->upper_idx = SFRAME.sort_to;                                              
    coro_this()->target = coro_this()->numbers[(SFRAME.sort_from + SFRAME.sort_to) / 2];  
#define TARGET coro_this()->target
#define LOWER coro_this()->numbers[coro_this()->lower_idx]
#define UPPER coro_this()->numbers[coro_this()->upper_idx]
    while (true) {
        while (LOWER < TARGET) {
            LOG_DEBUG_EXTRA("inc\t%ld[%lu]\t<=\t%ld", LOWER, coro_this()->lower_idx, TARGET);
            coro_this()->lower_idx++;                                                               coro_yield();
        }
        while (UPPER > TARGET) {;
            LOG_DEBUG_EXTRA("dec\t%ld[%lu]\t>=\t%ld", UPPER, coro_this()->upper_idx, TARGET);
            coro_this()->upper_idx--;                                                               coro_yield();
        }
        if (coro_this()->lower_idx >= coro_this()->upper_idx) {
            LOG_DEBUG_EXTRA("breaking");
            break;
        }
        LOG_DEBUG_EXTRA("before swap:\n%ld[%lu]\t%ld[]\t%ld[%lu]\n",
                        LOWER, coro_this()->lower_idx, TARGET, UPPER, coro_this()->upper_idx);
        coro_yield();
        AtomicSwap(&LOWER, &UPPER);
        coro_yield();
        coro_this()->lower_idx++;
        coro_this()->upper_idx--;
    }
    coro_yield();
#undef TARGET
#undef LOWER
#undef UPPER

    // Save on coro's stack because it may be overwritten after recursive call:
    SFRAME.sep_idx = coro_this()->upper_idx;                                                coro_yield();
    LOG_DEBUG_EXTRA("Separate idx = %lu", SFRAME.sep_idx);
    // Sort lower part:
    if (SFRAME.sep_idx > SFRAME.sort_from) {
        LOG_DEBUG_EXTRA("sort lower %lu:%lu", SFRAME.sort_from, SFRAME.sep_idx);
        coro_call(SortRange, SFRAME.sort_from, SFRAME.sep_idx);                             coro_yield();
    }
    
    // Sort upper part:
    if (SFRAME.sort_to > (SFRAME.sep_idx + 1)) {
        LOG_DEBUG_EXTRA("sort upper %lu:%lu", (SFRAME.sep_idx + 1), SFRAME.sort_to);
        coro_call(SortRange, (SFRAME.sep_idx + 1), SFRAME.sort_to);                         coro_yield();
    }
    coro_return();
}

void AtomicSwap(num_t *x, num_t *y)
{
    ASSERT(x != NULL);
    ASSERT(y != NULL);
    num_t temp = *x;
    *x = *y;
    *y = temp;
}

bool MergeFiles()
{
    int fd = open(O_FILE_NAME, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd == -1) {
        LOG_ERROR("can't create an output file");
        return false;
    }
    for (size_t i = 0; i < crt.coro_count; i++) {
        crt.coros[i].cur_pos = 0;
    }

    for (size_t i = 0; i < crt.total_n_numbers; i++) {
        size_t coro_idx;
        dprintf(fd, "%ld ", GetMinimalNumber(&coro_idx));
        if (coro_idx == (size_t) -1) {
            LOG_ERROR("numbers indexing out of range");
            close(fd);
            return false;
        }
    }
    close(fd);
    return true;
}

num_t GetMinimalNumber(size_t *coro_idx)
{
    size_t min_num_file_idx = 0;
    for (min_num_file_idx = 0; min_num_file_idx < crt.coro_count; min_num_file_idx++) {
        if (crt.coros[min_num_file_idx].cur_pos < crt.coros[min_num_file_idx].numbers_size) {
            // Got a first valid idx
            break;
        }
    }
    if (min_num_file_idx >= crt.coro_count) {
        *coro_idx = (size_t) -1;
        return 0;
    }
    num_t minimal = crt.coros[min_num_file_idx].numbers[crt.coros[min_num_file_idx].cur_pos];
    for (size_t i = min_num_file_idx + 1; i < crt.coro_count; i++) {
        if (crt.coros[i].cur_pos < crt.coros[i].numbers_size) {
            num_t current = crt.coros[i].numbers[crt.coros[i].cur_pos];
            if (minimal > current) {
                min_num_file_idx = i;
                minimal = current;
            }
        }
    }
    crt.coros[min_num_file_idx].cur_pos++;
    *coro_idx = min_num_file_idx;
    return minimal;
}

bool Free()
{
    ASSERT(crt.coros != NULL);
    for (size_t i = 0; i < crt.coro_count; i++) {
        free(crt.coros[i].numbers);
        free(crt.coros[i].stack);
        free((char*) crt.coros[i].aio_control.aio_buf);
        crt.coros[i].numbers = NULL;
        crt.coros[i].stack = NULL;
        crt.coros[i].aio_control.aio_buf = NULL;
        // It is implied that descriptors are already destroyed:
        ASSERT(crt.coros[i].aio_control.aio_fildes == -1);
    }

    free(crt.coros);
    crt.coros = NULL;
    return true;
}

void PrintStatistics(clock_t init_timestamp)
{
    printf("Time spent in co-routines:\n");
    clock_t clocks_per_usec = CLOCKS_PER_SEC / 1000000;
    ASSERT(clocks_per_usec != (clock_t) 0);
    for (size_t i = 0; i < crt.coro_count; i++) {
        size_t us = (size_t) (crt.coros[i].clocks_spent / clocks_per_usec);
        printf("--id = %2lu:\t%lu us\n", i, us);
    }
    size_t global_us = (size_t) ((clock() - init_timestamp) / clocks_per_usec);
    printf("\nTotal time spent:\t%lu us\n\n", global_us);
}
