#ifndef QUICKSORT_H
#define QUICKSORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>

#include "threadpool.h"

#define THRESHOLD_INSERTION_QUICK_SWITCH 64
#define THRESHOLD_LENGTH_FOR_NEW_TASK 100000

typedef enum SortType {
    DOUBLE,
    FLOAT,
    INT,
    INT8,
    INT16,
    INT32,
    INT64,
    UINT8,
    UINT16,
    UINT32,
    UINT64,
    CHARPTR_ARRAY,
    SPECIAL_STRUCT
} SortType;

typedef struct RecursiveSplitTaskWatcher {
    pthread_mutex_t watcherMutex;
    pthread_cond_t watcherConVar;
    atomic_int counter;
} RecursiveSplitTaskWatcher;

typedef struct RecursiveSplitTask {
    RecursiveSplitTaskWatcher *watcher;
    ThreadPool *pool;
    char *array;
    size_t byteSizeOfStruct;
    int64_t startingIndex;
    int64_t endingIndex;
    int8_t (*cmp) (void*, void*);
    SortType type;
} RecursiveSplitTask;

/**
 * @brief Performs a Quicksort for the respective type.
 * The Hoare partitioning scheme is used for dividing the array.
 * After hitting a specific threshold the sort scheme will switch to insertion sort.
 * The pivot element is always a random element of the array or of the specific partition.
 * Any type of array can be used, including specific structs (e.g., SPECIAL_STRUCT as the type).
 * Important: When sorting structs, a comparator must be provided!
 * For all other types, NULL can be passed instead, in which case a default comparator will be used.
 *
 * @param array array to be sorted
 * @param type type of array. Valid Types: DOUBLE, FLOAT, INT, INT8...INT64, UINT8...UINT64, CHARPTR_ARRAY, SPECIAL_STRUCT
 * @param byteSizeOfStruct necessary when sorting struct array, otherwise insert 0
 * @param length length of array
 * @param cmp comparator for specific type, when NOT SPECIAL_STRUCT and NULL is inserted, a default comparator will
 * be used
 */
void quicksort(void *array, SortType type, const size_t byteSizeOfStruct, const int64_t length, int8_t (*cmp) (void*, void*));

/**
 * @brief Performs a parallel Quicksort for the respective type.
 * The Hoare partitioning scheme is used for dividing the array.
 * After hitting a specific threshold the sort scheme will switch to insertion sort.
 * The pivot element is always a random element of the array or of the specific partition.
 * Any type of array can be used, including specific structs (e.g., SPECIAL_STRUCT as the type).
 * Important: When sorting structs, a comparator must be provided!
 * For all other types, NULL can be passed instead, in which case a default comparator will be used.
 *
 * @param array array to be sorted
 * @param type type of array. Valid Types: DOUBLE, FLOAT, INT, INT8...INT64, UINT8...UINT64, CHARPTR_ARRAY, SPECIAL_STRUCT
 * @param byteSizeOfStruct necessary when sorting struct array, otherwise insert 0
 * @param length length of array
 * @param cmp comparator for specific type, when NOT SPECIAL_STRUCT and NULL is inserted, a default comparator will
 * be used
 * @param pool the threadpool to parallelize the execution
 */
void parallelQuicksort(void *array, SortType type, const size_t byteSizeOfStruct, const int64_t length, int8_t (*cmp) (void*, void*),
    ThreadPool *pool);

// Byte sizes

#define SIZE_OF_DOUBLE sizeof(double)
#define SIZE_OF_FLOAT sizeof(float)
#define SIZE_OF_INT sizeof(int)
#define SIZE_OF_INT8 sizeof(int8_t)
#define SIZE_OF_INT16 sizeof(int16_t)
#define SIZE_OF_INT32 sizeof(int32_t)
#define SIZE_OF_INT64 sizeof(int64_t)
#define SIZE_OF_UINT8 sizeof(uint8_t)
#define SIZE_OF_UINT16 sizeof(uint16_t)
#define SIZE_OF_UINT32 sizeof(uint32_t)
#define SIZE_OF_UINT64 sizeof(uint64_t)
#define SIZE_OF_CHAR_PTR sizeof(char*)
#define SIZE_OF_RECURSIVE_SPLIT_TASK sizeof(RecursiveSplitTask)
#define SIZE_OF_RECURSIVE_SPLIT_TASK_WATCHER sizeof(RecursiveSplitTaskWatcher)


#endif
