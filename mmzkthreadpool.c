#include <assert.h>
#include <stdlib.h>
#include "mmzkthreadpool.h"

static inline int _join(mmzk_thread_pool_t *pool);
static inline int _start(mmzk_thread_pool_t *pool, unsigned int i);
static inline void *_work(mmzk_thread_pool_t *pool);

// Represents a task in a thread pool.
typedef struct mmzk_thread_pool_task {
  mmzk_worker_t worker;
  void *arg;
  struct mmzk_future *future;
} mmzk_thread_pool_task_t;

struct mmzk_linked_list_node {
  void *elem;
  struct mmzk_linked_list_node *prev;
  struct mmzk_linked_list_node *next;
};

// Repeatedly grab a task from the pool and execute it.
static void *thread_pool_work(void *thread_pool_arg)
{
  mmzk_thread_pool_t *pool = (mmzk_thread_pool_t *)thread_pool_arg;
  return _work(pool);
}

// Initialises the (fix-sized) thread pool from an array of threads.
// The length must be positive.
// Returns zero on success.
int mmzk_thread_pool_init(mmzk_thread_pool_t *pool, unsigned int length)
{
  assert(length > 0);

  int result;

  pthread_mutex_init(&pool->lock, NULL);
  pthread_cond_init(&pool->condition, NULL);
  pool->is_active = false;
  pool->thread_count = length;
  pool->threads = malloc(sizeof(pthread_t *) * length);
  if (!pool->threads)
    return 1;

  pool->threads[0] = malloc(sizeof(pthread_t) * length);
  if (!pool->threads[0]) {
    free(pool->threads);
    return 1;
  }

  // Setting up the empty work pool.
  pool->head = pool->tail = NULL;

  // Setting up the thread array and starts them working.
  for (unsigned int i = 0; i < length; i++) {
    pool->threads[i] = pool->threads[0] + i;
  }

  return result;
}

// Starts a thread pool.
// POOL must be either just initialised or shutted down and its threads joined,
// otherwise the behaviour is undefined.
// Returns 0 on success; 1 on failure where POOL is properly freed;
// -1 on other errors.
int mmzk_thread_pool_start(struct mmzk_thread_pool *pool) {
  assert(!pool->is_active);
  assert(!pool->head);

  int result;

  pool->is_active = true;
  pool->force_cancel = false;

  // Starts the threads.
  for (unsigned int i = 0; i < pool->thread_count; i++)
    result = _start(pool, i);

  return result;
}

// Shuts down a thread pool.
// If FORCE is false, POOL will stop taking new tasks, but the worker threads
// will not terminate until all tasks are completed. Otherwise they will
// terminate immediately after finishing up the current task, and all 
// non-executed tasks will not be executed.
// If JOIN is true, the function will wait until the termination of all worker
// threads.
// Returns 0 on success.
int mmzk_thread_pool_shutdown(mmzk_thread_pool_t *pool, bool force, bool join) 
{
  // Shut the pool down.
  pthread_mutex_lock(&pool->lock);
  pool->is_active = false;
  if (force)
    pool->force_cancel = true;
  pthread_cond_broadcast(&pool->condition);
  pthread_mutex_unlock(&pool->lock);

  if (join)
    return _join(pool);

  return 0;
}

// Wait for all threads in POOL to terminate.
// POOL must be shutted down, otherwise the behaviour is undefined.
// When FORCE is true, the function attempts to terminate the threads at once.
// In this case, any on-going task may end at anytime, potentially resulting in
// not properly releasing dynamically allocated resources.
// Returns 0 on success.
int mmzk_thread_pool_join(struct mmzk_thread_pool *pool) {
  assert(!pool->is_active);

  return _join(pool);
}

// Frees a thread pool.
// POOL must be shutted down and its threads joined, otherwise the behaviour is
// undefined.
void mmzk_thread_pool_free(struct mmzk_thread_pool *pool) {
  assert(!pool->is_active);
  
  mmzk_linked_list_node_t *cur;
  mmzk_thread_pool_task_t *task;
  while (pool->head) {
    cur = pool->head;
    pool->head = pool->head->next;
    task = cur->elem;
    free(task);
    free(cur);
  }

  free(pool->threads[0]);
  free(pool->threads);
}

// Executes the task by a thread from POOL.
// POOL must have started.
// Return 0 on success.
int mmzk_thread_pool_execute(struct mmzk_thread_pool *pool,
    mmzk_worker_t worker, void *arg, struct mmzk_future *future)
{
  assert(pool->is_active);

  // Create a task.
  mmzk_thread_pool_task_t *task = malloc(sizeof(mmzk_thread_pool_task_t));
  if (!task)
    return 1;

  task->worker = worker;
  task->arg = arg;
  if (future != NULL) {
    future->is_completed = false;
    pthread_mutex_init(&future->lock, NULL);
    pthread_cond_init(&future->condition, NULL);
  }
  task->future = future;
  mmzk_linked_list_node_t *task_node = malloc(sizeof(mmzk_linked_list_node_t));
  if (!task_node) {
    free(task);
    return 1;
  }

  // Insert the task.
  task_node->elem = task;
  task_node->next = NULL;
  pthread_mutex_lock(&pool->lock);
  task_node->prev = pool->tail;
  if (pool->tail != NULL) {
    pool->tail->next = task_node;
  } else {
    pool->head = task_node;
  }
  pool->tail = task_node;
  pthread_cond_signal(&pool->condition);
  pthread_mutex_unlock(&pool->lock);

  return 0;
}

// Waits for a future to finish and returns the result.
void *mmzk_await(struct mmzk_future *future) {
  pthread_mutex_lock(&future->lock);
  if (!future->is_completed)
   pthread_cond_wait(&future->condition, &future->lock);
  pthread_mutex_unlock(&future->lock);
  return future->result;
}

static inline int _join(mmzk_thread_pool_t *pool) {
  int result = 0;

  // Wait for threads to finish.
  for (unsigned int i = 0; i < pool->thread_count; i++) {
    if (pthread_join(*(pool->threads[i]), NULL))
      result = -1;
  }

  return result;
}

static inline int _start(mmzk_thread_pool_t *pool, unsigned int i) {
  if (pthread_create(pool->threads[i], NULL, &thread_pool_work, pool)) {
    pool->thread_count = i;
    if (mmzk_thread_pool_shutdown(pool, true, true)) {
      mmzk_thread_pool_free(pool);
      return -1;
    }
    mmzk_thread_pool_free(pool);
    return 1;
  }

  return 0;
}

static inline void *_work(mmzk_thread_pool_t *pool) {
  mmzk_thread_pool_task_t *task;
  mmzk_linked_list_node_t *task_node;

  while (true) {
    pthread_mutex_lock(&pool->lock);
    while (!pool->head) {
      if (!pool->is_active)
        goto job_finish;

      // Wait until a task is added into the pool.
      pthread_cond_wait(&pool->condition, &pool->lock);
    }

    // Grab a task from the front of the pool.
    task_node = pool->head;
    pool->head = pool->head->next;
    if (pool->head != NULL) {
      pool->head->prev = NULL;
    } else {
      pool->tail = NULL;
    }
    pthread_mutex_unlock(&pool->lock);
    task = task_node->elem;
    free(task_node);

    // Run the task.
    mmzk_future_t *future = task->future;
    void *result = (task->worker)(task->arg);
    free(task);
    if (future != NULL) {
      future->result = result;
      future->is_completed = true;
      pthread_mutex_lock(&future->lock);
      pthread_cond_signal(&future->condition);
      pthread_mutex_unlock(&future->lock);
    }

    if (pool->force_cancel)
      return NULL;
  }

job_finish:
  pthread_mutex_unlock(&pool->lock);
  return NULL;
}
