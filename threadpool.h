#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>

#define MAX_QUEUE_TASKS 8192
#define MAX_THREAD_FAIL_INITS 10

typedef struct Task {
    void (*taskFunction) (void*);
    void *taskObject;
} Task;

typedef struct TaskQueue {
    uint16_t currentTasks;
    uint16_t headIndex;
    uint16_t tailIndex;
    Task queue[MAX_QUEUE_TASKS];
} TaskQueue;

typedef struct ThreadMetaData {
    uint16_t tasksTaken;
    uint16_t tasksProcessed;
    uint16_t threadId;
} ThreadMetaData;

typedef struct Thread {
    TaskQueue internQueue;
    pthread_t internThread;
    ThreadMetaData metaData;
    pthread_mutex_t activeMutex;
    bool active;
} Thread;

typedef struct ThreadPool {
    TaskQueue queue;
    pthread_mutex_t queueMutex;
    pthread_cond_t condVar;
    pthread_cond_t blockedCondVar;
    uint8_t amountThreads;
    bool closed;
    Thread currentThreads[];
} ThreadPool;

typedef struct ThreadContext {
    ThreadPool *pool;
    Thread *currentThread;
} ThreadContext;

/**
 * @brief Initializes a thread pool with a given number of threads.
 * Important: The maximum number of threads that can run on the respective system, or what makes sense, is not checked.
 * This is the user’s responsibility.
 *
 * @param maxThreads max threads of the thread pool
 * @return the ThreadPool struct
 */
ThreadPool* initializeThreadPool(uint8_t maxThreads);

/**
 * @brief Frees the given thread pool.
 * The thread pool must be terminated first; otherwise, the memory cannot be freed.
 *
 * @param pool the thread pool to be freed
 * @return true -> if successfull, false -> if not successfull
 */
bool freeThreadPool(ThreadPool **pool);

/**
 * @brief Terminates the given thread pool by joining all threads in the pool.
 * After termination the thread pool can safely be freed.
 *
 * @param pool the thread pool to be terminated
 * @return true -> if successfull, false -> if not successfull
 */
bool terminateThreadPool(ThreadPool *pool);

/**
 * @brief Submits a specific task to the thread pool. The task is initially placed in the pool’s internal queue.
 * It then waits until a thread in the pool has capacity for a new task.
 * Each thread also has its own internal task queue.
 * Both queues are processed sequentially.
 *
 * @param pool the thread pool to which the task is submitted
 * @param taskFunction the function that should be executed within the task
 * @param taskInput the input that should be processed within the function
 * @param blockIfFull Specifies whether the producing thread should be blocked if the thread pool’s queue is full
 * true -> the producing thread waits, false -> the task is discarded
 * @return true -> if the task is successfully submitted, false -> if the task is not successfully submitted
 */
bool submitTaskToThreadPool(ThreadPool *pool, void (*taskFunction) (void*), void *taskInput, bool blockIfFull);

#define SIZE_OF_TASK sizeof(Task)
#define SIZE_OF_TASK_QUEUE sizeof(TaskQueue)
#define SIZE_OF_THREAD sizeof(Thread)
#define SIZE_OF_THREAD_POOL sizeof(ThreadPool)
#define SIZE_OF_THREAD_CONTEXT sizeof(ThreadContext)

#endif
