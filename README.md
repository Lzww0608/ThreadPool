### C++实现的简易动态线程池，主要特性如下：

+ 实现多线程安全的任务队列，线程池使用异步操作。
+ 使用atomic实现无锁线程安全队列。

+ 任务提交(submit)利用完美转发获取可调用对象的函数签名，lambda与function包装任务。

+ 使用RAII管理线程池的生命周期。


### 新增纯C版本动态线程池 ———— 2024.3.26

+ 基本逻辑与C++版本相同，实现细节略有不同，具体见代码注释，其中"atomic.h" 与 "spinlock.h"为直接使用动态开源库。 

+ 编译为动态链接库命令shell: gcc thread_pool.c -c -fPIC   gcc -shared thread_pool.o -o libthread_pool.so -I./ -L./ -lpthread

+ taskqueue_test.cc shell: g++ taskqueue_test.cc -o taskqueue_test -lgtest -lgtest_main -lpthread (需安装Google Test)

+ threadpool_test.cc shell: g++ -Wl,-rpath=./ threadpool_test.cc -o threadpool_test -I./ -L./ -lthreadpool -lpthread
