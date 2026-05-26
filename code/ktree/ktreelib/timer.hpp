#ifndef __TIMER_HPP__
#define __TIMER_HPP__

#include <iostream>
#include <chrono>

class Timer {
public:
	Timer() : running(false) {}
	void start() {
		if (running) {
			throw std::runtime_error("Timer is already running");
		}
		start_time = std::chrono::high_resolution_clock::now();
		running = true;
	}
	void stop() {
		if (!running) {
			throw std::runtime_error("Timer is not running");
		}
		end_time = std::chrono::high_resolution_clock::now();
		running = false;
	}
	void reset() {
		running = false;
	}

	friend std::ostream& operator<<(std::ostream& out, Timer& timer) {
		if (timer.running) {
			throw std::runtime_error("Timer is still running");
		}
		auto duration = std::chrono::duration<double>(timer.end_time - timer.start_time);
		out << duration.count() << "ms";
		return out;
	}

	std::string to_string() {
		if (running) {
			throw std::runtime_error("Timer is still running");
		}
		auto duration = std::chrono::duration<double>(end_time - start_time);
		return std::to_string(duration.count()) + " s";
	}

private:
	std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
	std::chrono::time_point<std::chrono::high_resolution_clock> end_time;
	bool running;
};

#endif