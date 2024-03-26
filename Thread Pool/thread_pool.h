#ifndef  _THREAD_POOL_H
#define _THREAD_POOL_H

typedef struct thread_pool_s thread_pool_t;
//ctx
typedef void (*handler_pt)(void*);

#ifdef __cplusplus
extern "C" 
{
#endif

thread_pool_t *thread_pool_create(int thread_count);

void thread_pool_terminate(thread_pool_t* pool);

int thread_pool_post (thread_pool_t *pool, handler_pt func, void *arg);

void thread_pool_waitdone(thread_pool_t *pool);



#ifdef __cplusplus
}
#endif

#endif // ! 