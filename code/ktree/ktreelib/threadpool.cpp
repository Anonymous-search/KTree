#include "threadpool.hpp"

#ifdef MULTITHREADED_ENABLED

#include "utils.hpp"

namespace THREADS {

void TaskQueue::push(KTREE::Node *node) {
	std::lock_guard<std::mutex> lock(mtx);
	queue.push(node);
	cv_.notify_all();
}

KTREE::Node *TaskQueue::pop() {
	std::unique_lock<std::mutex> lock(mtx);
	cv_.wait(lock, [this] { return !queue.empty(); });
	KTREE::Node *node = queue.front();
	queue.pop();
	return node;
}

bool TaskQueue::is_empty() {
	std::lock_guard<std::mutex> lock(mtx);
	return queue.empty();
}

ThreadPool::ThreadPool(size_t num_threads): stop(false), active_tasks(0) {
	if (num_threads == 0) {
		num_threads = std::thread::hardware_concurrency();
	}
	for (size_t i = 0; i < num_threads; i++) {
		workers.push_back(std::thread(&ThreadPool::worker, this));
	}
}

ThreadPool::~ThreadPool() {
	for (auto& worker: workers) {
		if (worker.joinable()) {
			worker.join();
		}
	}
}

void ThreadPool::add_task(KTREE::Node *node) {
	{
		std::lock_guard<std::mutex> lock(mtx);
		active_tasks++;
	}
	queue.push(node);
}

bool ThreadPool::has_active_tasks() {
	std::lock_guard<std::mutex> lock(mtx);
	return active_tasks > 0;
}

void ThreadPool::worker() {
	std::thread::id this_id = std::this_thread::get_id();
	
	while (true) {
		KTREE::Node* node = queue.pop();
		if (node == nullptr)
			break;
		node->split(node->getNum_points());
		if (node->getLeft() != nullptr) {
			add_task(node->getLeft());
		}
		if (node->getRight() != nullptr) {
			add_task(node->getRight());
		}
		{
			std::lock_guard<std::mutex> lock(mtx);
			active_tasks--;
		}
	}
}

void ThreadPool::wait_for_completion() {
	while (true) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));

		if (!has_active_tasks() && queue.is_empty()) {
			for (auto& worker: workers) {
				this->add_task(nullptr);
			}
			break;
		}
	}
}

};

#endif