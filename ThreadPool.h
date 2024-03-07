#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <time.h>
#include <vector>
#include <cassert>
#include <queue>
#include <future>
#include <mutex>
#include <queue>
#include <functional>
#include <future>
#include <thread>
#include <utility>
#include <vector>
#include <condition_variable>
#include <string>
#include <shared_mutex>

//构造安全队列
template <typename T>
struct SafeQueue
{
private:
	std::queue<T> safe_queue; 
	std::mutex _m; //互斥访问信号量

public:
	//返回队列是否为空
	bool empty() {
		std::unique_lock<std::mutex> lock(_m);
		return safe_queue.empty();
	}
	//返回队列长度
	size_t size() {
		std::unique_lock<std::mutex> lock(_m);
		return safe_queue.size();
	}
	//队列添加元素
	void push(T &t) {
		std::unique_lock<std::mutex> lock(_m);
		safe_queue.emplace(t);
	}
	//队列去除元素
	bool pop(T &t) {
		std::unique_lock<std::mutex> lock(_m);
		if (safe_queue.empty()) //判断队列是否为空
			return false;
		t = std::move(safe_queue.front()); //取出队首元素，队列中元素可能为uncopyable
		safe_queue.pop();
		return true;
	}

};



class ThreadPool {
private:
	//线程池是否关闭
	bool thread_shutdown;
	//线程安全队列，即任务队列
	SafeQueue<std::function<void()>> thread_queue;
	//线程工作队列
	std::vector<std::thread> m_threads;
	//线程休眠锁互斥变量
	std::mutex thread_conditional_mutex;
	//线程环境锁，让线程处于休眠或者唤醒状态
	std::condition_variable cv;

	//线程工作类
	class ThreadWorker {
	private:
		//线程id
		int thread_id;
		//所属线程池
		ThreadPool *thread_pool;

	public:
		ThreadWorker(ThreadPool *pool, const int id) : thread_pool(pool), thread_id(id) {}

		//重载()
		void operator()() {
			std::function<void()> func;

			bool is_poped = false;

			//循环从线程池中提取任务
			while (!thread_pool->thread_shutdown) {
				std::unique_lock<std::mutex> lock(thread_pool->thread_conditional_mutex);
				//任务队列为空则阻塞
				while (thread_pool->thread_queue.empty()) {
					thread_pool->cv.wait(lock);
				}
				//取出任务队列中的元素
				is_poped = thread_pool->thread_queue.pop(func);

				if (is_poped) {
					func();
				}
			}
		}
	};


public:

	ThreadPool(const int threads = 4): m_threads(std::vector<std::thread>(threads)), thread_shutdown(false) {
		init();
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool(ThreadPool&&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
	ThreadPool& operator=(ThreadPool&&) = delete;

	void init() {
		for (int i = 0; i < m_threads.size(); i++) {
			m_threads[i] = std::thread{ ThreadWorker(this, i) };
		}
	}

	template <typename F, typename ...Args>
	auto submit(F &&f, Args &&...args) -> std::future<decltype(f(args...))> {
		using return_type = decltype(f(args...));
		//绑定函数与传入参数
		std::function<return_type()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
		//封装函数至shared_ptr
		auto task_ptr = std::make_shared<std::packaged_task<return_type()>>(func);

		//将任务指针封装
		std::function<void()> task_wrapper = [task_ptr]() {
			(*task_ptr)();
		};

		assert(!thread_shutdown);
		//加入队列
		thread_queue.push(task_wrapper);
		//唤醒一个睡眠中的线程
		cv.notify_one();
		//返回任务指针
		return task_ptr->get_future();
	}

	~ThreadPool() {
		//保证先前提交的所有任务都已经完成
		auto f = submit([]() {});
		f.get();
		thread_shutdown = true;
		//唤醒所有工作线程
		cv.notify_all();

		for (auto& t : m_threads) {
			if (t.joinable()) {
				t.join();
			}
		}
	}
};

#endif

