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

//���찲ȫ����
template <typename T>
struct SafeQueue
{
private:
	std::queue<T> safe_queue; 
	std::mutex _m; //��������ź���

public:
	//���ض����Ƿ�Ϊ��
	bool empty() {
		std::unique_lock<std::mutex> lock(_m);
		return safe_queue.empty();
	}
	//���ض��г���
	size_t size() {
		std::unique_lock<std::mutex> lock(_m);
		return safe_queue.size();
	}
	//�������Ԫ��
	void push(T &t) {
		std::unique_lock<std::mutex> lock(_m);
		safe_queue.emplace(t);
	}
	//����ȥ��Ԫ��
	bool pop(T &t) {
		std::unique_lock<std::mutex> lock(_m);
		if (safe_queue.empty()) //�ж϶����Ƿ�Ϊ��
			return false;
		t = std::move(safe_queue.front()); //ȡ������Ԫ�أ�������Ԫ�ؿ���Ϊuncopyable
		safe_queue.pop();
		return true;
	}

};



class ThreadPool {
private:
	//�̳߳��Ƿ�ر�
	bool thread_shutdown;
	//�̰߳�ȫ���У����������
	SafeQueue<std::function<void()>> thread_queue;
	//�̹߳�������
	std::vector<std::thread> m_threads;
	//�߳��������������
	std::mutex thread_conditional_mutex;
	//�̻߳����������̴߳������߻��߻���״̬
	std::condition_variable cv;

	//�̹߳�����
	class ThreadWorker {
	private:
		//�߳�id
		int thread_id;
		//�����̳߳�
		ThreadPool *thread_pool;

	public:
		ThreadWorker(ThreadPool *pool, const int id) : thread_pool(pool), thread_id(id) {}

		//����()
		void operator()() {
			std::function<void()> func;

			bool is_poped = false;

			//ѭ�����̳߳�����ȡ����
			while (!thread_pool->thread_shutdown) {
				std::unique_lock<std::mutex> lock(thread_pool->thread_conditional_mutex);
				//�������Ϊ��������
				while (thread_pool->thread_queue.empty()) {
					thread_pool->cv.wait(lock);
				}
				//ȡ����������е�Ԫ��
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
		//�󶨺����봫�����
		std::function<return_type()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
		//��װ������shared_ptr
		auto task_ptr = std::make_shared<std::packaged_task<return_type()>>(func);

		//������ָ���װ
		std::function<void()> task_wrapper = [task_ptr]() {
			(*task_ptr)();
		};

		assert(!thread_shutdown);
		//�������
		thread_queue.push(task_wrapper);
		//����һ��˯���е��߳�
		cv.notify_one();
		//��������ָ��
		return task_ptr->get_future();
	}

	~ThreadPool() {
		//��֤��ǰ�ύ�����������Ѿ����
		auto f = submit([]() {});
		f.get();
		thread_shutdown = true;
		//�������й����߳�
		cv.notify_all();

		for (auto& t : m_threads) {
			if (t.joinable()) {
				t.join();
			}
		}
	}
};

#endif

