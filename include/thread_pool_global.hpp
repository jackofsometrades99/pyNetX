#ifndef THREAD_POOL_GLOBAL_H
#define THREAD_POOL_GLOBAL_H

void init_global_pool(int nThreads);
class ThreadPool; // forward-declare

ThreadPool& get_pool();

#endif
