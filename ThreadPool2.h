#ifndef THREAD_POOL2_H
#define THREAD_POOL2_H
//version2:使用atomic实现无锁队列（CAS操作会有自旋锁）
#include <vector>
#include <mutex>
#include <future>
#include <condition_variable>
#include <queue>
#include <thread>
#include <functional>
#include <shared_mutex>
#include <cassert>
#include <utility>

//使用列表来实现队列
template <typename T>
struct Node {
	T data;
	std::atomic<Node<T>*> next;

	Node(const T& _data) : data(_data), next(nullptr) {}
};

template <typename T>
class LockFreeQueue {
private:
	std::atomic<size_t> len;
	std::atomic<Node<T>*> head;
	std::atomic<Node<T>*> tail;

public:
	LockFreeQueue() {
		Node<T>* dummy = new Node<T>(T());
		head.store(dummy);
		tail.store(dummy);
	}

	LockFreeQueue(const LockFreeQueue&) = delete;
	LockFreeQueue(LockFreeQueue&&) = delete;
	LockFreeQueue& operator=(const LockFreeQueue&) = delete;
	LockFreeQueue& operator=(LockFreeQueue&&) = delete;

	size_t size();

	bool empty();

	void push(const T &t);

	bool pop(T &t);
};

template <typename T>
size_t LockFreeQueue<T>::size() {
	return len.load();
}

template <typename T>
bool LockFreeQueue<T>::empty() {
	return len.load() == 0;
}

template <typename T>
void LockFreeQueue<T>::push(const T &t) {
	Node<T>* new_node = new Node<T>(t);

	while (true) {
		Node<T>* tail_node = tail.load();
		Node<T>* next_node = tail_node->next.load();
		if (tail_node == tail.load()) {
			if (next_node == nullptr) {
				//CAS:原地更新队尾指针
				if (tail_node->next.compare_exchange_weak(next_node, new_node)) {
					tail.compare_exchange_weak(tail_node, new_node);
					len++;
					break;
				}
			}
			else {
				//移动队尾指针
				tail.compare_exchange_weak(tail_node, next_node);
			}
		}
	}
}

template <typename T>
bool LockFreeQueue<T>::pop(T &t) {
	if (empty())
		return false;
	while (true) {
		Node<T>* head_node = head.load();
		Node<T>* tail_node = tail.load();
		Node<T>* next_node = head_node->next.load();
		//确保加载后未被其他线程修改
		if (head_node == head.load()) {
			if (head_node == tail_node) {
				if (next_node == nullptr) {
					return false;
				}
				tail.compare_exchange_weak(tail_node, next_node);
			}
			else {
				t = std::move(next_node->data);
				if (head.compare_exchange_weak(head_node, next_node)) {
					len--;
					delete(head_node);
					return true;
				}
			}
		}
	}
}



class ThreadPool {
private:
	bool pool_shutdown;
	LockFreeQueue<std::function<void()>> task_queue;
	std::vector<std::thread> m_threads;
	std::mutex m_conditional_mutex;
	std::condition_variable cv;


	class ThreadWorker {
	private:
		ThreadPool* thread_workers;

	public:
		ThreadWorker(ThreadPool* threads): thread_workers(threads) {}

		void operator()() {
			std::function<void()> func;

			bool is_poped = false;

			while (!thread_workers->pool_shutdown) {
				std::unique_lock<std::mutex> lock(thread_workers->m_conditional_mutex);

				while (thread_workers->task_queue.empty()) {
					thread_workers->cv.wait(lock);
				}

				is_poped = thread_workers->task_queue.pop(func);

				if (is_poped) {
					func();
				}
			}
		}
	};


public:
	ThreadPool(const int threads = 4) : pool_shutdown(false), m_threads(std::vector<std::thread>(threads)) {
		init();
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool(ThreadPool&&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
	ThreadPool& operator=(ThreadPool&&) = delete;


	void init() {
		for (auto &t : m_threads) {
			t = std::thread{ ThreadWorker(this) };
		}
	}


	template <typename F, typename ...Args>
	auto submit(F &&f, Args &&...args) -> std::future<decltype(std::forward<F>(f)(std::forward<Args>(args)...))> {
		using return_type = decltype(std::forward<F>(f)(std::forward<Args>(args)...));

		std::function<return_type()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);

		auto task_ptr = std::make_shared<std::packaged_task<return_type()>>(func);

		std::function<void()> task_wrapper = [task_ptr]() {
			(*task_ptr)();
		};

		assert(!pool_shutdown);

		task_queue.push(task_wrapper);

		cv.notify_one();

		return task_ptr->get_future();

	}

	~ThreadPool() {
		auto f = submit([]() {});
		f.get();

		pool_shutdown = true;
		cv.notify_all();

		for (auto& t : m_threads) {
			if (t.joinable())
				t.join();
		}
	}

};



#endif
