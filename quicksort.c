#include "quicksort.h"

typedef enum ShiftDirection {
    LEFT,
    RIGHT
} ShiftDirection;

static void parallelRecursiveTask(void*);
static RecursiveSplitTask *buildRecursiveSplitTask(RecursiveSplitTaskWatcher*, ThreadPool*, char* , SortType,
    const size_t, const int64_t, const int64_t, int8_t (*) (void*, void*));
static bool freeRecursiveSplitTask(RecursiveSplitTask**);
static RecursiveSplitTaskWatcher *buildRecursiveSplitTaskWatcher(void);
static bool freeRecursiveSplitTaskWatcher(RecursiveSplitTaskWatcher **);

static void insertionSort(char*, SortType , const size_t, const int64_t, const int64_t, int8_t (*) (void* , void*));
static int8_t internInsertionCompare(char*, char*, const size_t, const int64_t, const int64_t, const int64_t,
    int8_t (*) (void*, void*));
static size_t getByteSizeOfSortType(SortType, const size_t);
static int64_t calculatePivotRandom(const int64_t, const int64_t);

static void quicksortIntern(char*, SortType, const size_t, const int64_t, const int64_t, int8_t (*cmp) (void*, void*));
static int64_t hoarePartition(char*, SortType, const size_t, const int64_t, const int64_t, int8_t (*cmp) (void*, void*));

static bool tryShift(char*, SortType, const size_t, int64_t*, const int64_t, int8_t (*cmp) (void*, void*), ShiftDirection);
static inline bool tryShiftPointer(char*, int64_t*, const void*, const size_t, int8_t (*cmp) (void*, void*), ShiftDirection);

static void swap(char*, SortType, const size_t, int64_t, int64_t);
static void swapMemoryInArray(char*, const size_t, int64_t, int64_t);

static int8_t (*getDefaultComparator(SortType)) (void*, void*);
static int8_t compareDoubles(void*, void*);
static int8_t compareFloats(void*, void*);
static int8_t compareInts(void*, void*);
static int8_t compareInts8(void*, void*);
static int8_t compareInts16(void*, void*);
static int8_t compareInts32(void*, void*);
static int8_t compareInts64(void*, void*);
static int8_t compareUInts8(void*, void*);
static int8_t compareUInts16(void*, void*);
static int8_t compareUInts32(void*, void*);
static int8_t compareUInts64(void*, void*);
static int8_t compareCharPtrs(void*, void*);

void quicksort(void *array, SortType type, const size_t byteSizeOfStruct,
    const int64_t length, int8_t (*cmp) (void*, void*)) {
    if((array == NULL) || (length == 0))
        return;
    if((type == SPECIAL_STRUCT) && ((byteSizeOfStruct == 0) || (cmp == NULL)))
        return;
    if(cmp == NULL) cmp = getDefaultComparator(type);

    char *tempArray = (char* ) array;
    quicksortIntern(tempArray, type, byteSizeOfStruct, 0, length - 1, cmp);
}

void parallelQuicksort(void *array, SortType type, const size_t byteSizeOfStruct,
    const int64_t length, int8_t (*cmp) (void*, void*), ThreadPool *pool) {
    if((array == NULL) || (length == 0) || (pool == NULL))
        return;
    if((type == SPECIAL_STRUCT) && ((byteSizeOfStruct == 0) || (cmp == NULL)))
        return;
    if(cmp == NULL) cmp = getDefaultComparator(type);

    char *tempArray = (char* ) array;
    RecursiveSplitTaskWatcher *taskWatcher = buildRecursiveSplitTaskWatcher();
    RecursiveSplitTask *taskInputStruct = buildRecursiveSplitTask(taskWatcher, pool, tempArray, type,
        byteSizeOfStruct, 0, length - 1, cmp);

    pthread_mutex_lock(&(taskWatcher->watcherMutex));
    submitTaskToThreadPool(pool, &parallelRecursiveTask, taskInputStruct, true);
    while (atomic_load(&(taskWatcher->counter)) != 0) {
        pthread_cond_wait(&(taskWatcher->watcherConVar), &(taskWatcher->watcherMutex));
    }
    pthread_mutex_unlock(&(taskWatcher->watcherMutex));
    freeRecursiveSplitTaskWatcher(&taskWatcher);
}

/**
 * @brief Executes the parallel Quicksort by splitting the array into new thread tasks until the array length falls
 * below THRESHOLD_LENGTH_FOR_NEW_TASK.
 * After that, the array is further split in the same thread until the length is below THRESHOLD_INSERTION_QUICK_SWITCH.
 *
 * @param input the next RecursiveSplitTask
 */
static void parallelRecursiveTask(void *input) {
    if(input == NULL)
        return;

    RecursiveSplitTask *task = (RecursiveSplitTask* ) input;
    RecursiveSplitTaskWatcher *watcher = (RecursiveSplitTaskWatcher* ) task->watcher;
    const int64_t length = (task->endingIndex - task->startingIndex) + 1;

    if(length <= THRESHOLD_INSERTION_QUICK_SWITCH) {
        insertionSort(task->array, task->type, task->byteSizeOfStruct, task->startingIndex,
            task->endingIndex, task->cmp);
    } else {
        atomic_fetch_add(&(watcher->counter), 2);
        const int64_t partitionIndex = hoarePartition(task->array, task->type, task->byteSizeOfStruct,
            task->startingIndex, task->endingIndex, task->cmp);

        RecursiveSplitTask *leftTask = buildRecursiveSplitTask(task->watcher, task->pool, task->array,
            task->type, task->byteSizeOfStruct, task->startingIndex, partitionIndex, task->cmp);
        RecursiveSplitTask *rightTask = buildRecursiveSplitTask(task->watcher, task->pool, task->array,
            task->type, task->byteSizeOfStruct, partitionIndex + 1, task->endingIndex, task->cmp);

        if(length <= THRESHOLD_LENGTH_FOR_NEW_TASK) {
            parallelRecursiveTask(leftTask);
            parallelRecursiveTask(rightTask);
        } else {
            submitTaskToThreadPool(task->pool, &parallelRecursiveTask, leftTask, true);
            submitTaskToThreadPool(task->pool, &parallelRecursiveTask, rightTask, true);
        }
    }

    freeRecursiveSplitTask(&task);

    if(atomic_fetch_sub(&(watcher->counter), 1) == 1) {
        pthread_cond_signal(&(watcher->watcherConVar));
    }
}

/**
 * @brief Builds a RecursiveSplitTask.
 * Its function is to continuously pass relevant data for the Quicksort in the form of a recursive task.
 *
 * @param watcher specific RecursiveSplitTaskWatcher for the task
 * @param pool threadpool to execute the specific recursive tasks
 * @param array array to be sorted
 * @param type type of array. Valid Types: DOUBLE, FLOAT, INT, INT8...INT64, UINT8...UINT64, CHARPTR_ARRAY, SPECIAL_STRUCT
 * @param byteSizeOfStruct necessary when sorting struct array, otherwise insert 0
 * @param startingIndex marks the index from which the task should begin
 * @param endingIndex marks the index up to which the task should be performed
 * @param cmp comparator for specific type, when NOT SPECIAL_STRUCT and NULL is inserted, a default comparator will
 * be used
 * @return the specific RecursiveSplitTask struct
 */
static RecursiveSplitTask *buildRecursiveSplitTask(RecursiveSplitTaskWatcher *watcher, ThreadPool *pool, char *array,
    SortType type, const size_t byteSizeOfStruct, const int64_t startingIndex, const int64_t endingIndex,
    int8_t (*cmp) (void*, void*)) {
    RecursiveSplitTask *result = (RecursiveSplitTask* ) malloc(SIZE_OF_RECURSIVE_SPLIT_TASK);
    if(result == NULL)
        return NULL;

    result->watcher = watcher;
    result->pool = pool;
    result->array = array;
    result->type = type;
    result->byteSizeOfStruct = byteSizeOfStruct;
    result->startingIndex = startingIndex;
    result->endingIndex = endingIndex;
    result->cmp = cmp;

    return result;
}

/**
 * @brief Frees the memory of a RecursiveSplitTask.
 *
 * @param destTask task to be freed
 * @return true -> if successfull, false -> if not successfull
 */
static bool freeRecursiveSplitTask(RecursiveSplitTask **destTask) {
    if((destTask == NULL) || (*destTask == NULL))
        return false;

    free(*destTask);
    *destTask = NULL;
    return true;
}

/**
 * @brief Builds a RecursiveSplitTaskWatcher.
 * Its function is to check whether all tasks have been executed and,
 * in the end, whether the entire array has been correctly merged.
 *
 * @return the specific RecursiveSplitTaskWatcher struct
 */
static RecursiveSplitTaskWatcher *buildRecursiveSplitTaskWatcher(void) {
    RecursiveSplitTaskWatcher *result = (RecursiveSplitTaskWatcher* ) malloc(SIZE_OF_RECURSIVE_SPLIT_TASK_WATCHER);
    if(result == NULL)
        return NULL;

    atomic_init(&(result->counter), 1);
    pthread_mutex_init(&(result->watcherMutex), NULL);
    pthread_cond_init(&(result->watcherConVar), NULL);

    return result;
}

/**
 * @brief Frees the memory of the RecursiveSplitTaskWatcher.
 *
 * @param destWatcher watcher to be freed
 * @return true -> if successfull, false -> if not successfull
 */
static bool freeRecursiveSplitTaskWatcher(RecursiveSplitTaskWatcher **destWatcher) {
    if((destWatcher == NULL) || (*destWatcher == NULL))
        return false;

    free(*destWatcher);
    *destWatcher = NULL;
    return true;
}

/**
 * @brief Sorts the given array using insertion sort.
 * The sorting is performed in-place.
 *
 * @param array array to be sorted
 * @param type type of array. Valid Types: DOUBLE, FLOAT, INT, INT8...INT64, UINT8...UINT64, CHARPTR_ARRAY, SPECIAL_STRUCT
 * @param byteSizeOfStruct necessary when sorting struct array, otherwise insert 0
 * @param startingIndex marks the index from which sorting should begin
 * @param endingIndex marks the index up to which sorting should be performed
 * @param cmp comparator for specific type, when NOT SPECIAL_STRUCT and NULL is inserted, a default comparator will
 * be used
 */
static void insertionSort(char *array, SortType type, const size_t byteSizeOfStruct, const int64_t startingIndex,
    const int64_t endingIndex, int8_t (*cmp) (void* , void*)) {
    const int64_t startIndex = startingIndex + 1;
    const size_t byteSize = getByteSizeOfSortType(type, byteSizeOfStruct);
    char *buffer = (char* ) malloc(byteSize);
    if(buffer == NULL)
        return;

    for(int64_t i = startIndex; i <= endingIndex; i++) {
        for(int64_t j = (i - 1); j >= startingIndex; j--) {
            const int8_t internInsertionCompareResult = internInsertionCompare(array, buffer, byteSize,
                startingIndex, i, j, cmp);

            if(internInsertionCompareResult == 0)
                continue;
            else
                break;
        }
    }

    free(buffer);
    buffer = NULL;
}

static int8_t internInsertionCompare(char *array, char *buffer, const size_t byteSize, const int64_t border,
    const int64_t currentIndex, const int64_t comparedIndex, int8_t (*cmp) (void*, void*)) {
    char *currentIndexPtr = array + ((uint64_t) currentIndex * byteSize);
    char *comparedIndexPtr = array + ((uint64_t) comparedIndex * byteSize);
    const int8_t comparedResult = cmp(currentIndexPtr, comparedIndexPtr);

    int8_t result;
    if(comparedResult == 1) {
        memcpy(buffer, currentIndexPtr, byteSize);
        memmove((comparedIndexPtr + (2 * byteSize)), (comparedIndexPtr + byteSize),
            ((uint64_t) (currentIndex - comparedIndex - 1)) * byteSize);
        memcpy((comparedIndexPtr + byteSize), buffer, byteSize);
        result = 1;
    } else if(comparedResult == -1) {
        if(comparedIndex == border) {
            memcpy(buffer, currentIndexPtr, byteSize);
            memmove((comparedIndexPtr + byteSize), comparedIndexPtr,
                ((uint64_t) (currentIndex - comparedIndex)) * byteSize);
            memcpy(comparedIndexPtr, buffer, byteSize);
            result = 1;
        } else {
            result = 0;
        }
    } else {
        memcpy(buffer, currentIndexPtr, byteSize);
        memmove((comparedIndexPtr + byteSize), comparedIndexPtr,
            ((uint64_t) (currentIndex - comparedIndex)) * byteSize);
        memcpy(comparedIndexPtr, buffer, byteSize);
        result = 1;
    }

    return result;
}

/**
 * @brief Get the byte size of sort type object.
 * Only necessary for insertion sort.
 *
 * @param type type of array. Valid Types: DOUBLE, FLOAT, INT, INT8...INT64, UINT8...UINT64, CHARPTR_ARRAY, SPECIAL_STRUCT
 * @param byteSizeOfStruct necessary when sorting struct array, otherwise insert 0
 * @return size of the sort type in byte
 */
static size_t getByteSizeOfSortType(SortType type, const size_t byteSizeOfStruct) {
    switch(type) {
        case DOUBLE:
            return SIZE_OF_DOUBLE;

        case FLOAT:
            return SIZE_OF_FLOAT;

        case INT:
            return SIZE_OF_INT;

        case INT8:
            return SIZE_OF_INT8;

        case INT16:
            return SIZE_OF_INT16;

        case INT32:
            return SIZE_OF_INT32;

        case INT64:
            return SIZE_OF_INT64;

        case UINT8:
            return SIZE_OF_UINT8;

        case UINT16:
            return SIZE_OF_UINT16;

        case UINT32:
            return SIZE_OF_UINT32;

        case UINT64:
            return SIZE_OF_UINT64;

        case CHARPTR_ARRAY:
            return SIZE_OF_CHAR_PTR;

        case SPECIAL_STRUCT:
            return byteSizeOfStruct;
    }
}

/**
 * @brief Calculates a random pivot in the range of the provided startingindex and endingindex.
 *
 * @param startingIndex marks the index from which the random index should be calculated
 * @param endingIndex marks the index up to which the random index should be calculated
 * @return the random pivot index
 */
static int64_t calculatePivotRandom(const int64_t startingIndex, const int64_t endingIndex) {
    const int64_t result = rand() % (endingIndex - startingIndex + 1) + startingIndex;

    return result;
}

/**
 * @brief Recursively calls itself and performing partitioning using the Hoare scheme along the way
 * until the length hits the THRESHOLD_INSERTION_QUICK_SWITCH (100 000) to switch to insertion sort.
 * @param array array to be sorted
 * @param type type of array. Valid Types: DOUBLE, FLOAT, INT, INT8...INT64, UINT8...UINT64, CHARPTR_ARRAY, SPECIAL_STRUCT
 * @param byteSizeOfStruct necessary when sorting struct array, otherwise insert 0
 * @param startingIndex the index from where the partition begins
 * @param endingIndex the index where the partition ends
 * @param cmp comparator for specific type, when NOT SPECIAL_STRUCT and NULL is inserted, a default comparator will
 * be used
 */
static void quicksortIntern(char *array, SortType type, const size_t byteSizeOfStruct,
    const int64_t startingIndex, const int64_t endingIndex, int8_t (*cmp) (void*, void*)) {
    const int64_t length = (endingIndex - startingIndex) + 1;
    if(length <= THRESHOLD_INSERTION_QUICK_SWITCH) {
        insertionSort(array, type, byteSizeOfStruct, startingIndex, endingIndex, cmp);
    } else {
        const int64_t partitionIndex = hoarePartition(array, type, byteSizeOfStruct, startingIndex, endingIndex, cmp);

        quicksortIntern(array, type, byteSizeOfStruct, startingIndex, partitionIndex, cmp);
        quicksortIntern(array, type, byteSizeOfStruct, partitionIndex + 1, endingIndex, cmp);
    }
}

/**
 * @brief Used to partition and sort the underlying array.
 * All elements smaller than the pivot element are placed on the left,
 * and all elements greater than the pivot element are placed on the right.
 * The first element will always be the pivot element.
 * @param array array to be partitioned
 * @param type type of array. Valid Types: DOUBLE, FLOAT, INT, INT8...INT64, UINT8...UINT64, CHARPTR_ARRAY, SPECIAL_STRUCT
 * @param byteSizeOfStruct necessary when sorting struct array, otherwise insert 0
 * @param startingIndex the index from where the partition begins
 * @param endingIndex the index where the partition ends
 * @param cmp comparator for specific type, when NOT SPECIAL_STRUCT and NULL is inserted, a default comparator will
 * be used
 * @return partition index for next partition
 */
static int64_t hoarePartition(char *array, SortType type, const size_t byteSizeOfStruct,
    const int64_t startingIndex, const int64_t endingIndex, int8_t (*cmp) (void*, void*)) {
    int64_t pivotIndex = calculatePivotRandom(startingIndex, endingIndex);
    if(pivotIndex != startingIndex) {
        swap(array, type, byteSizeOfStruct, pivotIndex, startingIndex);
        pivotIndex = startingIndex;
    }

    int64_t pointerLeft = (startingIndex - 1);
    int64_t pointerRight = (endingIndex + 1);

    bool pointerLeftMoving = true;
    bool pointerRightMoving = true;

    while(true) {
        do {
            pointerLeftMoving = tryShift(array, type, byteSizeOfStruct, &pointerLeft, pivotIndex, cmp, RIGHT);
        } while(pointerLeftMoving);

        do {
            pointerRightMoving = tryShift(array, type, byteSizeOfStruct, &pointerRight, pivotIndex, cmp, LEFT);
        } while(pointerRightMoving);

        if(pointerLeft >= pointerRight)
            return pointerRight;

        swap(array, type, byteSizeOfStruct, pointerLeft, pointerRight);
        pointerLeftMoving = false;
        pointerRightMoving = false;
    }
}

/**
 * @brief Attempts to perform a shift of one of the two pointers used in the Hoare algorithm.
 * @param array array to be partitioned
 * @param type type of array. Valid Types: DOUBLE, FLOAT, INT, INT8...INT64, UINT8...UINT64, CHARPTR_ARRAY, SPECIAL_STRUCT
 * @param byteSizeOfStruct necessary when sorting struct array, otherwise insert 0
 * @param ptrIndex index of left or right pointer
 * @param pivotIndex index of pivot value/element
 * @param cmp comparator for specific type, when NOT SPECIAL_STRUCT and NULL is inserted, a default comparator will
 * be used
 * @param shiftDirection direction of shift e.g. left Pointer -> performs "left shift", right pointer -> performs
 * "right shift"
 * @return true -> if pointer can move further, false -> if pointer can not move further
 */
static bool tryShift(char *array, SortType type, const size_t byteSizeOfStruct,
    int64_t *ptrIndex, const int64_t pivotIndex, int8_t (*cmp) (void*, void*),
    ShiftDirection shiftDirection) {
    if(shiftDirection == RIGHT) {
        *ptrIndex += 1;
    } else if(shiftDirection == LEFT) {
        *ptrIndex -= 1;
    }

    switch (type) {
    case DOUBLE: {
        const size_t pivotPos = ((size_t) pivotIndex * SIZE_OF_DOUBLE);
        const void *pivotElement = array + pivotPos;

        return tryShiftPointer(array, ptrIndex, pivotElement, SIZE_OF_DOUBLE, cmp, shiftDirection);
    }

    case FLOAT: {
        const size_t pivotPos = ((size_t) pivotIndex * SIZE_OF_FLOAT);
        const void *pivotElement = array + pivotPos;

        return tryShiftPointer(array, ptrIndex, pivotElement, SIZE_OF_FLOAT, cmp, shiftDirection);
    }

    case INT: {
        const size_t pivotPos = ((size_t) pivotIndex * SIZE_OF_INT);
        const void *pivotElement = array + pivotPos;

        return tryShiftPointer(array, ptrIndex, pivotElement, SIZE_OF_INT, cmp, shiftDirection);
    }

    case INT8: {
        const size_t pivotPos = ((size_t) pivotIndex * SIZE_OF_INT8);
        const void *pivotElement = array + pivotPos;

        return tryShiftPointer(array, ptrIndex, pivotElement, SIZE_OF_INT8, cmp, shiftDirection);
    }

    case INT16: {
        const size_t pivotPos = ((size_t) pivotIndex * SIZE_OF_INT16);
        const void *pivotElement = array + pivotPos;

        return tryShiftPointer(array, ptrIndex, pivotElement, SIZE_OF_INT16, cmp, shiftDirection);
    }

    case INT32: {
        const size_t pivotPos = ((size_t) pivotIndex * SIZE_OF_INT32);
        const void *pivotElement = array + pivotPos;

        return tryShiftPointer(array, ptrIndex, pivotElement, SIZE_OF_INT32, cmp, shiftDirection);
    }

    case INT64: {
        const size_t pivotPos = ((size_t) pivotIndex * SIZE_OF_INT64);
        const void *pivotElement = array + pivotPos;

        return tryShiftPointer(array, ptrIndex, pivotElement, SIZE_OF_INT64, cmp, shiftDirection);
    }

    case UINT8: {
        const size_t pivotPos = ((size_t) pivotIndex * SIZE_OF_UINT8);
        const void *pivotElement = array + pivotPos;

        return tryShiftPointer(array, ptrIndex, pivotElement, SIZE_OF_UINT8, cmp, shiftDirection);
    }

    case UINT16: {
        const size_t pivotPos = ((size_t) pivotIndex * SIZE_OF_UINT16);
        const void *pivotElement = array + pivotPos;

        return tryShiftPointer(array, ptrIndex, pivotElement, SIZE_OF_UINT16, cmp, shiftDirection);
    }

    case UINT32: {
        const size_t pivotPos = ((size_t) pivotIndex * SIZE_OF_UINT32);
        const void *pivotElement = array + pivotPos;

        return tryShiftPointer(array, ptrIndex, pivotElement, SIZE_OF_UINT32, cmp, shiftDirection);
    }

    case UINT64: {
        const size_t pivotPos = ((size_t) pivotIndex * SIZE_OF_UINT64);
        const void *pivotElement = array + pivotPos;

        return tryShiftPointer(array, ptrIndex, pivotElement, SIZE_OF_UINT64, cmp, shiftDirection);
    }

    case CHARPTR_ARRAY: {
        const size_t pivotPos = ((size_t) pivotIndex * SIZE_OF_CHAR_PTR);
        const void *pivotElement = array + pivotPos;

        return tryShiftPointer(array, ptrIndex, pivotElement, SIZE_OF_CHAR_PTR, cmp, shiftDirection);
    }

    case SPECIAL_STRUCT: {
        const size_t pivotPos = ((size_t) pivotIndex * byteSizeOfStruct);
        const void *pivotElement = array + pivotPos;

        return tryShiftPointer(array, ptrIndex, pivotElement, byteSizeOfStruct, cmp, shiftDirection);
    }

    default:
        exit(EXIT_FAILURE);
    }
}

static inline bool tryShiftPointer(char *array, int64_t *ptrIndex, const void *pivotElement,
    const size_t byteSize, int8_t (*cmp) (void*, void*), ShiftDirection shiftDirection) {
    const size_t elementPos = (((size_t) *ptrIndex) * byteSize);
    const void* ptrCurrentElement = array + elementPos;

    const bool result = (shiftDirection == RIGHT) ? cmp((void*) pivotElement, (void*) ptrCurrentElement) > 0 :
        cmp((void*) pivotElement, (void*) ptrCurrentElement) < 0;

    return result;
}

/**
 * @brief Performs a swap of two elements in the array, provided they meet the conditions for
 * swapping according to the Hoare algorithm.
 * @param array array, where the swap happens
 * @param type type of array. Valid Types: DOUBLE, FLOAT, INT, INT8...INT64, UINT8...UINT64, CHARPTR_ARRAY, SPECIAL_STRUCT
 * @param byteSizeOfStruct necessary when sorting struct array, otherwise insert 0
 * @param index1 index of first element to be swapped
 * @param index2 index of second element to be swapped
 */
static void swap(char *array, SortType type, const size_t byteSizeOfStruct, int64_t index1, int64_t index2) {
    switch (type) {
    case DOUBLE: {
        swapMemoryInArray(array, SIZE_OF_DOUBLE, index1, index2);
        break;
    }

    case FLOAT: {
        swapMemoryInArray(array, SIZE_OF_FLOAT, index1, index2);
        break;
    }

    case INT: {
        swapMemoryInArray(array, SIZE_OF_INT, index1, index2);
        break;
    }

    case INT8: {
        swapMemoryInArray(array, SIZE_OF_INT8, index1, index2);
        break;
    }

    case INT16: {
        swapMemoryInArray(array, SIZE_OF_INT16, index1, index2);
        break;
    }

    case INT32: {
        swapMemoryInArray(array, SIZE_OF_INT32, index1, index2);
        break;
    }

    case INT64: {
        swapMemoryInArray(array, SIZE_OF_INT64, index1, index2);
        break;
    }

    case UINT8: {
        swapMemoryInArray(array, SIZE_OF_UINT8, index1, index2);
        break;
    }

    case UINT16: {
        swapMemoryInArray(array, SIZE_OF_UINT16, index1, index2);
        break;
    }

    case UINT32: {
        swapMemoryInArray(array, SIZE_OF_UINT32, index1, index2);
        break;
    }

    case UINT64: {
        swapMemoryInArray(array, SIZE_OF_UINT64, index1, index2);
        break;
    }

    case CHARPTR_ARRAY: {
        swapMemoryInArray(array, SIZE_OF_CHAR_PTR, index1, index2);
        break;
    }

    case SPECIAL_STRUCT: {
        swapMemoryInArray(array, byteSizeOfStruct, index1, index2);
        break;
    }

    default:
        exit(EXIT_FAILURE);
    }
}

static void swapMemoryInArray(char* array, const size_t byteSize, int64_t index1, int64_t index2) {
    char buffer[byteSize];

    const size_t elementPosOne = ((size_t) index1 * byteSize);
    const size_t elementPosTwo = ((size_t) index2 * byteSize);

    memcpy(buffer, (array + elementPosOne), byteSize);
    memcpy((array + elementPosOne), (array + elementPosTwo), byteSize);
    memcpy((array + elementPosTwo), buffer, byteSize);
}

/**
 * @brief Get the default comparator function pointer.
 * @param type type of array. Valid Types: DOUBLE, FLOAT, INT, INT8...INT64, UINT8...UINT64, CHARPTR_ARRAY, SPECIAL_STRUCT
 * @return function pointer to the default comparator
 */
int8_t (*getDefaultComparator(SortType type)) (void*, void*) {
    switch (type)
    {
    case DOUBLE:
        return &compareDoubles;
    case FLOAT:
        return &compareFloats;
    case INT:
        return &compareInts;
    case INT8:
        return &compareInts8;
    case INT16:
        return &compareInts16;
    case INT32:
        return &compareInts32;
    case INT64:
        return &compareInts64;
    case UINT8:
        return &compareUInts8;
    case UINT16:
        return &compareUInts16;
    case UINT32:
        return &compareUInts32;
    case UINT64:
        return &compareUInts64;
    case CHARPTR_ARRAY:
        return &compareCharPtrs;
    default:
        exit(EXIT_FAILURE);
    }
}

static int8_t compareDoubles(void *pivotPtr, void *compareElementPtr) {
    const double d1Value = *((double* ) pivotPtr);
    const double d2Value = *((double* ) compareElementPtr);

    return (d1Value < d2Value) ? -1 : (d1Value == d2Value) ? 0 : 1;
}

static int8_t compareFloats(void *pivotPtr, void *compareElementPtr) {
    const float d1Value = *((float* ) pivotPtr);
    const float d2Value = *((float* ) compareElementPtr);

    return (d1Value < d2Value) ? -1 : (d1Value == d2Value) ? 0 : 1;
}

static int8_t compareInts(void *pivotPtr, void *compareElementPtr) {
    const int d1Value = *((int* ) pivotPtr);
    const int d2Value = *((int* ) compareElementPtr);

    return (d1Value < d2Value) ? -1 : (d1Value == d2Value) ? 0 : 1;
}

static int8_t compareInts8(void *pivotPtr, void *compareElementPtr) {
    const int8_t d1Value = *((int8_t* ) pivotPtr);
    const int8_t d2Value = *((int8_t* ) compareElementPtr);

    return (d1Value < d2Value) ? -1 : (d1Value == d2Value) ? 0 : 1;
}

static int8_t compareInts16(void *pivotPtr, void *compareElementPtr) {
    const int16_t d1Value = *((int16_t* ) pivotPtr);
    const int16_t d2Value = *((int16_t* ) compareElementPtr);

    return (d1Value < d2Value) ? -1 : (d1Value == d2Value) ? 0 : 1;
}

static int8_t compareInts32(void *pivotPtr, void *compareElementPtr) {
    const int32_t d1Value = *((int32_t* ) pivotPtr);
    const int32_t d2Value = *((int32_t* ) compareElementPtr);

    return (d1Value < d2Value) ? -1 : (d1Value == d2Value) ? 0 : 1;
}

static int8_t compareInts64(void *pivotPtr, void *compareElementPtr) {
    const int64_t d1Value = *((int64_t* ) pivotPtr);
    const int64_t d2Value = *((int64_t* ) compareElementPtr);

    return (d1Value < d2Value) ? -1 : (d1Value == d2Value) ? 0 : 1;
}

static int8_t compareUInts8(void *pivotPtr, void *compareElementPtr){
    const uint8_t d1Value = *((uint8_t* ) pivotPtr);
    const uint8_t d2Value = *((uint8_t* ) compareElementPtr);

    return (d1Value < d2Value) ? -1 : (d1Value == d2Value) ? 0 : 1;
}

static int8_t compareUInts16(void *pivotPtr, void *compareElementPtr) {
    const uint16_t d1Value = *((uint16_t* ) pivotPtr);
    const uint16_t d2Value = *((uint16_t* ) compareElementPtr);

    return (d1Value < d2Value) ? -1 : (d1Value == d2Value) ? 0 : 1;
}

static int8_t compareUInts32(void *pivotPtr, void *compareElementPtr) {
    const uint32_t d1Value = *((uint32_t* ) pivotPtr);
    const uint32_t d2Value = *((uint32_t* ) compareElementPtr);

    return (d1Value < d2Value) ? -1 : (d1Value == d2Value) ? 0 : 1;
}

static int8_t compareUInts64(void *pivotPtr, void *compareElementPtr) {
    const uint64_t d1Value = *((uint64_t* ) pivotPtr);
    const uint64_t d2Value = *((uint64_t* ) compareElementPtr);

    return (d1Value < d2Value) ? -1 : (d1Value == d2Value) ? 0 : 1;
}

static int8_t compareCharPtrs(void *pivotPtr, void *compareElementPtr) {
    char **pivotElement = (char** ) pivotPtr;
    char **comparedElement = (char** ) compareElementPtr;

    const int comparedValue = strcmp(*pivotElement, *comparedElement);
    return (comparedValue < 0) ? -1 : (comparedValue == 0) ? 0 : 1;
}
