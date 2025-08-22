#include <stdio.h>
#include <time.h>

#include "quicksort.h"

#define ARRAY_SIZE 100000000

void diffTimeSpecs(struct timespec*, struct timespec*, struct timespec*);

int main() {
    double *array = malloc(ARRAY_SIZE * SIZE_OF_DOUBLE);

    for(int i = 0; i < ARRAY_SIZE; i++) {
        const double randomValue = rand() % 100 + 1;
        array[i] = randomValue;
    }

    double *copyArray = malloc(ARRAY_SIZE * SIZE_OF_DOUBLE);
    memcpy(copyArray, array, ARRAY_SIZE * SIZE_OF_DOUBLE);

    // struct timespec before1, after1, result1;
    // clock_gettime(CLOCK_REALTIME, &before1);
    // quicksort(copyArray, DOUBLE, 0, ARRAY_SIZE, NULL);
    // clock_gettime(CLOCK_REALTIME, &after1);

    // diffTimeSpecs(&before1, &after1, &result1);
    // double resultTime1 = result1.tv_sec + (result1.tv_nsec / 1000000000.0);
    // printf("%fs\n", resultTime1);

    // for(int i = 0; i < ARRAY_SIZE; i++) {
    //     printf("%f\n", copyArray[i]);
    // }

    struct timespec before, after, result;

    ThreadPool *pool = initializeThreadPool(4);
    clock_gettime(CLOCK_REALTIME, &before);
    parallelQuicksort(array, DOUBLE, 0, ARRAY_SIZE, NULL, pool);
    clock_gettime(CLOCK_REALTIME, &after);

    double lastValue = array[0];
    int64_t count = 0;
    for(int i = 1; i < ARRAY_SIZE; i++) {
        const double value = array[i];
        if(value == lastValue) {
            count++;
        } else {
            printf("%f counted: %lu\n", lastValue, count);
            lastValue = value;
            count = 1;
        }
    }

    diffTimeSpecs(&before, &after, &result);
    double resultTime = result.tv_sec + (result.tv_nsec / 1000000000.0);
    printf("%fs\n", resultTime);
    return 0;
}

void diffTimeSpecs(struct timespec *spec1, struct timespec *spec2, struct timespec *result) {
    int64_t calculatedSeconds = (spec2->tv_sec - spec1->tv_sec);
    long calculatedNanoseconds = (spec2->tv_nsec - spec1->tv_nsec);
    if(calculatedNanoseconds < 0) {
        result->tv_sec = calculatedSeconds - 1;
        result->tv_nsec = calculatedNanoseconds + 1000000000;
    } else {
        result->tv_sec = calculatedSeconds;
        result->tv_nsec = calculatedNanoseconds;
    }
}
