#include "threadpool.h"

static void *threadTicking(void*);

static bool initThread(ThreadPool*, Thread*);
static void initThreadMetaData(ThreadMetaData*, uint16_t);
static void incrementMetaDataTaskField(uint16_t*);
static bool cancelThreads(ThreadPool*, const uint8_t);
static bool turnThreadsInactive(ThreadPool*, const uint8_t);
static bool turnThreadActive(Thread*);
static bool turnThreadInactive(Thread*);
static bool isThreadActive(Thread*);
static ThreadContext *initThreadContext(ThreadPool*, Thread*);
static bool freeThreadContext(ThreadContext**);
static void initTaskQueue(TaskQueue*);

static bool taskQueueEmpty(TaskQueue*);
static bool taskQueueFull(TaskQueue*, const uint16_t);
static void putTaskIntoQueue(TaskQueue*, Task*);
static bool getNextTask(TaskQueue*, Task*);
static void increaseRingBufferIndex(uint16_t*, const uint16_t);

ThreadPool *initializeThreadPool(uint8_t maxThreads) {
    if(maxThreads == 0)
        return NULL;

    const size_t byteSize = SIZE_OF_THREAD_POOL + (SIZE_OF_THREAD * maxThreads);
    ThreadPool *resultPool = (ThreadPool *) malloc(byteSize);
    if(resultPool == NULL)
        return NULL;

    initTaskQueue(&(resultPool->queue));
    pthread_cond_init(&(resultPool->blockedCondVar), NULL);

    pthread_mutex_init(&(resultPool->queueMutex), NULL);
    pthread_cond_init(&(resultPool->condVar), NULL);

    resultPool->amountThreads = maxThreads;
    resultPool->closed = false;

    uint8_t failedTries = 0;
    for(uint8_t i = 0; i < maxThreads; i++) {
        const bool success = initThread(resultPool, &(resultPool->currentThreads[i]));
        if(!success) {
            failedTries++;
            if(failedTries == MAX_THREAD_FAIL_INITS) {
                cancelThreads(resultPool, i);
                free(resultPool);
                return NULL;
            }

            i--;
        }
    }

    return resultPool;
}

bool freeThreadPool(ThreadPool **pool) {
    if((pool == NULL) || (*pool == NULL))
        return false;

    if(!(*pool)->closed)
        return false;

    free(*pool);
    *pool = NULL;
    return true;
}

bool terminateThreadPool(ThreadPool *pool) {
    if((pool == NULL) || (pool->closed))
        return false;

    const uint8_t threadsAmount = pool->amountThreads;
    cancelThreads(pool, threadsAmount);
    pool->closed = true;
    return true;
}

bool submitTaskToThreadPool(ThreadPool *pool, void (*taskFunction) (void*), void *taskInput, bool blockIfQueueFull) {
    if((pool == NULL) || (taskFunction == NULL))
        return false;

    if(pool->closed)
        return false;

    const Task newTask = {.taskFunction = taskFunction,
                          .taskObject = taskInput};

    pthread_mutex_lock(&(pool->queueMutex));
    while (taskQueueFull(&(pool->queue), MAX_QUEUE_TASKS)) {
        if(!blockIfQueueFull) {
            pthread_mutex_unlock(&(pool->queueMutex));
            return false;
        }

        pthread_cond_wait(&(pool->blockedCondVar), &(pool->queueMutex));
    }

    putTaskIntoQueue(&(pool->queue), (Task* ) &newTask);
    pthread_cond_signal(&(pool->condVar));
    pthread_mutex_unlock(&(pool->queueMutex));
    return true;
}

/**
 * @brief Main execution loop for each worker thread in the thread pool.
 * Each thread continuously checks its own internal queue and the global pool queue for tasks.
 * If tasks are available, they are executed sequentially.
 * Threads wait on a condition variable if no tasks are present, and exit gracefully when the thread pool is terminated.
 * This function also updates thread metadata, including the number of tasks taken and processed.
 *
 * @param input ThreadContext struct including the specific thread struct and the thread pool struct
 * @return always NULL
 */
static void *threadTicking(void *input) {
    if(input == NULL)
        pthread_exit(NULL);

    ThreadContext *contextOfThread = (ThreadContext* ) input;
    Thread *currentThread = contextOfThread->currentThread;
    ThreadMetaData *currentThreadMetaData = &(currentThread->metaData);
    ThreadPool *currentThreadPool = contextOfThread->pool;

    while (true) {
        pthread_mutex_lock(&(currentThreadPool->queueMutex));

        while ((taskQueueEmpty(&(currentThread->internQueue))) &&
                taskQueueEmpty(&(currentThreadPool->queue)) &&
                isThreadActive(currentThread)) {
            pthread_cond_wait(&(currentThreadPool->condVar), &(currentThreadPool->queueMutex));
        }

        bool threadActive = isThreadActive(currentThread);
        if((!threadActive) && (taskQueueEmpty(&(currentThread->internQueue))) &&
            taskQueueEmpty(&(currentThreadPool->queue))) {
            pthread_mutex_unlock(&(currentThreadPool->queueMutex));
            break;
        }

        Task currentTask = {0};
        bool gotTask = false;
        if(!taskQueueFull(&(currentThread->internQueue), MAX_QUEUE_TASKS)) {
            gotTask = getNextTask(&(currentThreadPool->queue), &currentTask);
            if(gotTask) {
                pthread_cond_signal(&(currentThreadPool->blockedCondVar));
                incrementMetaDataTaskField(&(currentThreadMetaData->tasksTaken));
            }
        }

        pthread_mutex_unlock(&(currentThreadPool->queueMutex));

        if(gotTask)
            putTaskIntoQueue(&(currentThread->internQueue), &currentTask);

        Task workTask = {0};
        if(getNextTask(&(currentThread->internQueue), &workTask)) {
            workTask.taskFunction(workTask.taskObject);
            incrementMetaDataTaskField(&(currentThreadMetaData->tasksProcessed));
        }
    }

    freeThreadContext(&contextOfThread);
    pthread_exit(NULL);
}

/**
 * @brief Initializes a thread of a specific thread pool.
 *
 * @param pool the thread pool where the thread should be initialized
 * @param destThread the thread that should be initialized
 * @return true -> if thread initialization was successfull,
 * false -> if thread initialization was not successfull
 */
static bool initThread(ThreadPool *pool, Thread *destThread) {
    static uint16_t currentThreadId = 0;
    initThreadMetaData(&(destThread->metaData), currentThreadId++);
    initTaskQueue(&(destThread->internQueue));
    pthread_mutex_init(&(destThread->activeMutex), NULL);
    destThread->active = false;

    ThreadContext *context = initThreadContext(pool, destThread);
    if(context == NULL)
        return false;

    if((pthread_create(&(destThread->internThread), NULL, &threadTicking, (void* ) context)) != 0) {
        freeThreadContext(&context);
        perror("It seems like there was a problem initializing the thread");
        return false;
    }

    const bool threadTurnedActive = turnThreadActive(destThread);
    if(!threadTurnedActive) {
        freeThreadContext(&context);
    }

    return true;
}

/**
 * @brief Initializes the meta data of a thread.
 * The meta data contains the thread ID, the tasks taken by the specific thread and the tasks
 * processed by the specific thread.
 *
 * @param destMetaData the meta data that should be initialized
 * @param threadId the thread ID that should be used for initialization
 */
static void initThreadMetaData(ThreadMetaData *destMetaData, uint16_t threadId) {
    destMetaData->threadId = threadId;
    destMetaData->tasksTaken = 0;
    destMetaData->tasksProcessed = 0;
}

/**
 * @brief Increments a data field of metadata.
 *
 * @param metaDataField pointer to the field that should be incremented
 */
static void incrementMetaDataTaskField(uint16_t *metaDataField) {
    *metaDataField += 1;
}

/**
 * @brief Intern method to cancel a specific amount of threads in the threadpool by joining them.
 * It will always start from the first thread in the thread pool.
 *
 * @param pool the thread pool where the threads should be cancelled
 * @param threadsIndexAmount the amount of threads that should be cancelled, starting by the first thread
 * @return true -> if all threads are cancelled successfully, false -> if not all threads are cancelled
 * successfully
 */
static bool cancelThreads(ThreadPool *pool, const uint8_t threadsIndexAmount) {
    if (!turnThreadsInactive(pool, threadsIndexAmount)) {
        perror("It seems like there occurred an error while toggling the threads inactive");
        return false;
    }

    bool result = true;
    for(uint8_t i = 0; i < threadsIndexAmount; i++) {
        Thread *currentThread = &(pool->currentThreads[i]);
        if(pthread_join(currentThread->internThread, NULL) != 0) {
            perror("It seems like a thread wasnt able to join");
            result = false;
        }
    }

    return result;
}

/**
 * @brief Intern method to turn the intern state of a specific amount of threads in the threadpool to inactive.
 * It will always start from the first thread in the thread pool.
 *
 * @param pool the thread pool where the threads should be turned inactive
 * @param threadIndexAmount the amount of threads that should be turned inactive, starting by the first thread
 * @return true -> if all threads are turned inactive successfully, false -> if not all threads are turned inactive
 * successfully
 */
static bool turnThreadsInactive(ThreadPool *pool, const uint8_t threadIndexAmount) {
    for(uint8_t i = 0; i < threadIndexAmount; i++) {
        const Thread *currentThread = &(pool->currentThreads[i]);
        if (!turnThreadInactive((Thread* ) currentThread)) {
            perror("It seems like one thread cannot be turned inactive");
        }
    }

    pthread_cond_broadcast(&(pool->condVar));
    return true;
}

/**
 * @brief Intern methode to change the intern running state of a specific thread to active.
 *
 * @param destThread the thread that should be turned active
 * @return true -> if the thread is turned active successfully, false -> if the thread is not turned active
 */
static bool turnThreadActive(Thread *destThread) {
    if(destThread == NULL)
        return false;

    bool result = false;
    pthread_mutex_lock(&(destThread->activeMutex));
    if(!destThread->active) {
        destThread->active = true;
        result = true;
    }
    pthread_mutex_unlock(&(destThread->activeMutex));

    return result;
}

/**
 * @brief Intern methode to change the intern running state of a specific thread to inactive.
 *
 * @param destThread the thread that should be turned inactive
 * @return true -> if the thread is turned inactive successfully, false -> if the thread is not turned inactive
 */
static bool turnThreadInactive(Thread *destThread) {
    if(destThread == NULL)
        return false;

    bool result = false;
    pthread_mutex_lock(&(destThread->activeMutex));
    if(destThread->active) {
        destThread->active = false;
        result = true;
    }
    pthread_mutex_unlock(&(destThread->activeMutex));

    return result;
}

/**
 * @brief Intern method to check the intern running state of a specific thread.
 *
 * @param destThread the thread that should be checked
 * @return the intern running state of the thread
 */
static bool isThreadActive(Thread *destThread) {
    bool result = false;
    pthread_mutex_lock(&(destThread->activeMutex));
    result = destThread->active;
    pthread_mutex_unlock(&(destThread->activeMutex));
    return result;
}

/**
 * @brief Initializes a intern thread context struct, which holds pointers to the specific thread struct and the
 * thread pool struct.
 * The thread context struct is necessary for the threadTicking method.
 *
 * @param pool the pointer to the specific thread pool
 * @param destThread the pointer to the specific thread
 * @return the thread context struct
 */
static ThreadContext *initThreadContext(ThreadPool *pool, Thread *destThread) {
    ThreadContext *resultContext = (ThreadContext* ) malloc(SIZE_OF_THREAD_CONTEXT);
    if(resultContext == NULL)
        return NULL;

    resultContext->pool = pool;
    resultContext->currentThread = destThread;

    return resultContext;
}

/**
 * @brief Frees the specified ThreadContext struct.
 *
 * @param destContext the ThreadContext to be freed
 * @return true -> if ThreadContext is successfully freed, false -> if ThreadContext is not successfully freed
 */
static bool freeThreadContext(ThreadContext **destContext) {
    if((destContext == NULL) || (*destContext == NULL))
        return false;

    free(*destContext);
    *destContext = NULL;
    return true;
}

/**
 * @brief Initializes the specific task queue.
 * It can be used for the task queue of a specific thread or of a specific thread pool.
 * The task queue will always be a circular buffer.
 *
 * @param destQueue the queue that should be initialized
 */
static void initTaskQueue(TaskQueue *destQueue) {
    destQueue->currentTasks = 0;
    destQueue->headIndex = 0;
    destQueue->tailIndex = 0;
    for(int i = 0; i < MAX_QUEUE_TASKS; i++) {
        destQueue->queue[i] = (Task) {0};
    }
}

/**
 * @brief Checks if the task queue is empty.
 *
 * @param destQueue the queue that should be checked
 * @return true -> if empty, false -> if not empty
 */
static bool taskQueueEmpty(TaskQueue *destQueue) {
    return (destQueue->currentTasks == 0);
}

/**
 * @brief Checks if the task queue is full.
 *
 * @param destQueue the queue that should be checked
 * @param maxSize the max size of a task queue, e.g. MAX_QUEUE_TASKS
 * @return true -> if full, false -> if not full
 */
static bool taskQueueFull(TaskQueue *destQueue, const uint16_t maxSize) {
    return (destQueue->currentTasks == maxSize);
}

/**
 * @brief Puts a specific task struct into the specific task queue.
 *
 * @param destQueue the queue where the task should be inserted
 * @param srcTask the task that should be inserted
 */
static void putTaskIntoQueue(TaskQueue *destQueue, Task *srcTask) {
    if(taskQueueFull(destQueue, MAX_QUEUE_TASKS))
        return;

    Task *nextTask = &(destQueue->queue[destQueue->headIndex]);
    nextTask->taskFunction = srcTask->taskFunction;
    nextTask->taskObject = srcTask->taskObject;
    destQueue->currentTasks += 1;
    increaseRingBufferIndex(&(destQueue->headIndex), MAX_QUEUE_TASKS);
}

/**
 * @brief Gets the next task struct from the specific task queue.
 *
 * @param destQueue the queue where the task should be taken from
 * @param destTask the pointer to the task struct where the fields of the specific task should be
 * copied inside
 * @return true -> if there is a next task in the specific queue, false -> if there is no task in the specific queue
 */
static bool getNextTask(TaskQueue *destQueue, Task *destTask) {
    if(taskQueueEmpty(destQueue))
        return false;

    const Task *nextTask = &(destQueue->queue[destQueue->tailIndex]);
    if(nextTask == NULL)
        return false;

    destTask->taskFunction = nextTask->taskFunction;
    destTask->taskObject = nextTask->taskObject;
    destQueue->currentTasks -= 1;
    increaseRingBufferIndex(&(destQueue->tailIndex), MAX_QUEUE_TASKS);
    return true;
}

/**
 * @brief Increases the specific index (e.g. head/tail) of the circular buffer.
 * All queues are equivalent to a circular buffer.
 *
 * @param indexPtr the pointer that should be increased
 * @param maxSize the max size of the circular buffer, e.g. MAX_QUEUE_TASKS
 */
static void increaseRingBufferIndex(uint16_t *indexPtr, const uint16_t maxSize) {
    uint16_t nextIndex = *indexPtr + 1;
    if(nextIndex == maxSize)
        nextIndex = 0;

    *indexPtr = nextIndex;
}
