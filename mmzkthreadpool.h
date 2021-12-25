#ifndef MMZK_THREAD_POOL_H
#define MMZK_THREAD_POOL_H

#include <pthread.h>
#include <stdbool.h>

typedef void *(*mmzk_worker_t)(void *);

typedef struct mmzk_linked_list_node mmzk_linked_list_node_t;

// We will use this thread pool to implement sending every pixel to a thread.
typedef struct mmzk_thread_pool {
  mmzk_linked_list_node_t *head;
  mmzk_linked_list_node_t *tail;
  pthread_mutex_t lock;
  pthread_cond_t condition;
  pthread_t **threads;
  unsigned int thread_count;
  bool is_active;
  bool force_cancel;
} mmzk_thread_pool_t;

// Contains the "future" of the result of a task. Can invoke mmzk_await() on it
// to wait for the task to finish and get the result.
// No need to initialise it explicitly.
// Contains the "future" of the result of a task. Can invoke mmzk_await() on it
// to wait for the task to finish and get the result.
// There is no need to initialise it explicitly, but once a future is used,
// it CANNOT be reused unless it has been waited.
typedef struct mmzk_future {
  pthread_mutex_t lock;
  pthread_cond_t condition;
  void *result;
  bool is_completed;
} mmzk_future_t;

// Initialises a (fix-sized) thread pool from an array of threads. The pool
// is not started after calling this function and requires 
// mmzk_thread_pool_start() to start it.
// The length must be positive.
// Returns 0 on success; 1 on failure where POOL is properly freed;
// -1 on other errors.
int mmzk_thread_pool_init(struct mmzk_thread_pool *pool, unsigned int length);

// Shuts down a thread pool.
// If FORCE is false, POOL will stop taking new tasks, but the worker threads
// will not terminate until all tasks are completed. Otherwise they will
// terminate immediately after finishing up the current task, and all 
// non-executed tasks will not be executed.
// If JOIN is true, the function will wait until the termination of all worker
// threads.
// Returns 0 on success.
int mmzk_thread_pool_shutdown(struct mmzk_thread_pool *pool, bool force, 
    bool join);

// Wait for all threads in POOL to terminate.
// POOL must be shutted down, otherwise the behaviour is undefined.
// Returns 0 on success.
int mmzk_thread_pool_join(struct mmzk_thread_pool *pool);

// Starts a thread pool.
// POOL must be either just initialised or shutted down and its threads joined, 
// otherwise the behaviour is undefined.
// Returns 0 on success; 1 on failure where POOL is properly freed;
// -1 on other errors.
int mmzk_thread_pool_start(struct mmzk_thread_pool *pool);

// Frees a thread pool.
// POOL must be shutted down and its threads joined, otherwise the behaviour is
// undefined.
void mmzk_thread_pool_free(struct mmzk_thread_pool *pool);

// Executes the task by a thread from POOL.
// POOL must have started.
// If FUTURE is not NULL, assign it with a future of the result of this task.
// Return 0 on success.
int mmzk_thread_pool_execute(struct mmzk_thread_pool *pool,
    mmzk_worker_t worker, void *arg, struct mmzk_future *future);

// Waits for a future to finish and returns the result.
void *mmzk_await(struct mmzk_future *future);

#endif /* MMZK_THREAD_POOL_H */
