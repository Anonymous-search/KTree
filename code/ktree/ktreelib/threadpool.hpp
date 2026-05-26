#ifndef __THREADPOOL_HPP__
#define __THREADPOOL_HPP__

#ifdef MULTITHREADED_ENABLED
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

#include "ktree.hpp"

namespace THREADS {

class TaskQueue {
private:
	std::queue<KTREE::Node *> queue;
	std::mutex mtx;
	std::condition_variable cv_;
public:
	TaskQueue() = default;
	~TaskQueue() = default;

	void push(KTREE::Node *node);
	KTREE::Node *pop();
	bool is_empty();
};

class ThreadPool {
private:
	std::vector<std::thread> workers;
	TaskQueue queue;
	std::atomic<size_t> active_tasks;
	std::mutex mtx;
	bool stop;

public:
	ThreadPool(size_t num_threads = 0);
	~ThreadPool();

	void add_task(KTREE::Node *node);
	bool has_active_tasks();

	void wait_for_completion();

private:
	void worker();
};

}

#endif

#endif