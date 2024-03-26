#include <stdatomic.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include "spinlock.h"
#include "atomic.h"
#include "thread_pool.h"

typedef struct spinlock spinlock_t;

typedef struct task_s {
    void *next;      //链接下一个任务
    handler_pt func; //封装任务函数
    void *argc;     //堆上进行分配
} task_t; 

typedef struct task_queue_s {
    void *head;
    void **tail;
    int block; //1为阻塞 0为非阻塞
    spinlock_t lock;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} task_queue_t;

typedef struct thread_pool_s {
    task_queue_t *task_queue;
    atomic_int quit;
    int thread_count;
    pthread_t *threads;
} thread_pool_t;

// static接口的可见性只能在源文件中可见

// 对称 有创建就有销毁

// 资源的创建 回滚式编程
// 业务逻辑   防御式编程
static task_queue_t *__task_queue_create() {
    int ret;
    task_queue_t *queue = (task_queue_t *)malloc(sizeof(task_queue_t)); // (task_queue_t *)malloc(sizeof(*queue))
    //假设成功
    if (queue) {
        //初始化
        ret = pthread_mutex_init(&queue->mutex, NULL);
        if (ret == 0) {
            ret = pthread_cond_init(&queue->cond, NULL);
            if (ret == 0) {
                //用户态行为，一定会成功
                spinlock_init(&queue->lock);
                queue->head = NULL;
                queue->tail = &queue->head;
                queue->block = 1; //阻塞类型
                return queue;
            }
            pthread_mutex_destroy(&queue->mutex);
        }
        free(queue);
    }
    return NULL;
}

static void __nonblock(task_queue_t *queue) {
    pthread_mutex_lock(&queue->mutex);
    queue->block = 0;   //设置非阻塞，关闭线程池
    pthread_mutex_unlock(&queue->mutex);
    pthread_cond_broadcast(&queue->cond);   //唤醒所有的线程
}

static inline void __add_task(task_queue_t *queue, task_t *task) {
    //不限定任务类型，只要任务起始地址是一个用于连接的指针
    //泛用性：支持不同类型的任务
    void **link = (void **)task;
    *link = NULL;

    spinlock_lock(&queue->lock);
    *queue->tail /*等价于 queue->tail->next */= link; //next指向下一个task
    queue->tail = link;                              // tail指向最后一个task
    spinlock_unlock(&queue->lock);
    pthread_cond_signal(&queue->cond);
}


//task API


//防御式编程：不满足条件直接return
static inline void* __pop_task(task_queue_t *queue) {
    spinlock_lock(&queue->lock);
    if (queue->head == NULL) {
        spinlock_unlock(&queue->lock);
        return NULL;
    }
    task_t *task;
    task = queue->head;

    void **link = (void **)task;
    queue->head = *link/*task->next*/;

    if (queue->head == NULL) {
        //逻辑完备性
        queue->tail = &queue->head;
    }
    spinlock_unlock(&queue->lock);
    return task;
}


static inline void* __get_task(task_queue_t *queue) {
    task_t *task;
    // 会存在虚假唤醒问题：signal发送时，实际并没有任务
    while ((task = __pop_task(queue)) == NULL) {
        pthread_mutex_lock(&queue->mutex);
        if (queue->block == 0) {
            pthread_mutex_unlock(&queue->mutex);
            return NULL;
        }
        // 1.先unlock(&mtx)
        // 2.休眠
        // 3.add_task: signal/broadcast后唤醒
        // 4.加上lock(&mtx)
        pthread_cond_wait(&queue->cond, &queue->mutex);
        pthread_mutex_unlock(&queue->mutex);
    }
    return task;
}


static void __task_queue_destroy(task_queue_t *queue) {
    task_t *task;
    while ((task = __pop_task(queue))) {
        free(task);
    }
    spinlock_destroy(&queue->lock);
    pthread_cond_destroy(&queue->cond);
    pthread_mutex_destroy(&queue->mutex);
    free(queue);
}

//threads API
static void* __thread_pool_worker(void *arg) {
    thread_pool_t *pool = (thread_pool_t*)arg;
    task_t *task;
    void *ctx;

    while (atomic_load(&pool->quit) == 0) {
        task = (task_t*)__get_task(pool->task_queue);
        if (!task) break;
        handler_pt func = task->func;
        ctx = task->argc;
        free(task);
        func(ctx);
    }

    return NULL;
}

static void __threads_terminate(thread_pool_t* pool) {
    atomic_store(&pool->quit, 1);
    __nonblock(pool->task_queue);
    int i;
    for (i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }
}

static int __threads_create(thread_pool_t *pool, size_t thread_count) {
    pthread_attr_t attr;
    int ret;

    ret = pthread_attr_init(&attr);
    if (ret == 0) {
        pool->threads = (pthread_t*)malloc(sizeof(pthread_t*) * thread_count);
        if (pool->threads) {
            int i = 0;
            for (; i < thread_count; i++) {
                if (pthread_create(&pool->threads[i], &attr, __thread_pool_worker, pool) != 0) {
                    break;
                }
            }
            pool->thread_count = i;
            pthread_attr_destroy(&attr);
            if (i == thread_count) {
                return 0;
            }
            __threads_terminate(pool);
            free(pool->threads);
        }
        ret = -1;
    }
    return ret;
}




//thread_pool API

//回滚式编程
thread_pool_t *thread_pool_create(int thread_count) {
    thread_pool_t *pool;
    pool = (thread_pool_t*)malloc(sizeof(*pool));
    if (pool) {
        task_queue_t *queue = __task_queue_create();
        if (queue) {
            pool->task_queue = queue;
            atomic_init(&pool->quit, 0);
            if ((__threads_create(pool, thread_count)) == 0) {
                return pool;
            }
            __task_queue_destroy(queue);
        }
        free(pool);
    }
    return NULL;
}

//防御式编程
int thread_pool_post(thread_pool_t *pool, handler_pt func, void *arg) {
    if (atomic_load(&pool->quit) == 1) {
        return -1;//创建失败
    }
    task_t *task = (task_t*)malloc(sizeof(task_t));
    if (!task) return -1;
    task->func = func;
    task->argc = arg;
    __add_task(pool->task_queue, task);
    return 0;
}

void thread_pool_terminate(thread_pool_t *pool) {
    atomic_store(&pool->quit, 1);
    __nonblock(pool->task_queue);
}

void thread_pool_waitdone(thread_pool_t *pool) {
    int i;
    for (i = 0; i < pool->thread_count; i++) {
        pthread_join(&pool->threads[i], NULL);
    }
    __task_queue_destroy(pool->task_queue);
    free(pool->threads);
    free(pool);
}
