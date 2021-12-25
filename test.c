#define ARR_LEN 10000
#define SEGMENT 8

#include <stdlib.h>
#include <stdio.h>
#include "mmzkthreadpool.h"

int arr[ARR_LEN];
mmzk_future_t future[SEGMENT];
pthread_mutex_t lock;

struct section_sum {
  int start;
  int end;
};

static void *sum_worker(void *section_sum_arg) {
  struct section_sum *section_sum = section_sum_arg;
  int *r = malloc(sizeof(int));
  *r = 0;
  for (int i = section_sum->start; i < section_sum->end; i++)
    *r += arr[i];

  return r;
}

int main() {
  int result = 0;
  pthread_mutex_init(&lock, NULL);
  mmzk_thread_pool_t pool;
  mmzk_thread_pool_init(&pool, 8);
  struct section_sum workers[SEGMENT];
  for (int i = 0; i < ARR_LEN; i++)
    arr[i] = i + 1;
  for (int i = 0; i < SEGMENT; i++) {
    workers[i].start = i * ARR_LEN / SEGMENT;
    workers[i].end = (i + 1) * ARR_LEN / SEGMENT;
  }

  mmzk_thread_pool_start(&pool);
  for (int i = 0; i < SEGMENT; i++)
    mmzk_thread_pool_execute(&pool, &sum_worker, &workers[i], &future[i]);
  for (int i = 0; i < SEGMENT; i++) {
    int *sub_result = mmzk_await(&future[i]);
    result += *sub_result;
    free(sub_result);
  }
  printf("Sum: %d\n", result);
  mmzk_thread_pool_shutdown(&pool, false, false);
  mmzk_thread_pool_free(&pool);
}
