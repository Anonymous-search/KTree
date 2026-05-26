#ifndef __UTILS_HPP__
#define __UTILS_HPP__


#include <string>
#include <sstream>
#include <vector>
#include <iostream>

#include <numeric>
#include <algorithm>

namespace KTREE {
	
bool dir_exists(const std::string& path);
bool create_dir(const std::string& path);


enum LogLevel {
	DEBUG,
	INFO,
	WARNING,
	ERROR,
};

class Logger {

public:
	Logger(LogLevel _level = LogLevel::DEBUG);
	virtual ~Logger();

	template <typename T>
	Logger& operator<< (const T& value) {
		_buffer << value;
		return *this;
	}

public:
	static LogLevel log_level;
	const LogLevel& get_log_level();
protected:
	std::ostringstream _buffer;
	Logger& operator=(const Logger& logger) = delete;
private:
	LogLevel level;
};

template<class ForwardIt>
ForwardIt min_element(ForwardIt first, ForwardIt last) {
	if (first == last) {
		return last;
	}
	ForwardIt smallest = first;

	while(++first != last){
		if (*first < *smallest) {
			smallest = first;
		}
	}
	return smallest;
}

template<class ForwardIt>
ForwardIt max_element(ForwardIt first, ForwardIt last) {
	if (first == last) {
		return last;
	}
	ForwardIt largest = first;

	while(++first != last){
		if (*first > *largest) {
			largest = first;
		}
	}
	return largest;
}


// math help functions

template <typename V>
void compute_variance(const std::vector<V* >& data, std::vector<float>& variance) {
	// calculate the variance over the axis 0

	for (size_t i = 0; i < data[0]->size(); i++) {
		float sum = 0.0;
		float sum_squared = 0.0;
		for (size_t j = 0; j < data.size(); j++) {
			sum += (*(data[j]))[i];
			sum_squared += (*(data[j]))[i] * (*(data[j]))[i];
		}
		float mean = sum / data.size();
		variance[i] = (sum_squared / data.size()) - (mean * mean);
	}
}

template<typename T>
void argsort(const std::vector<T>& v, std::vector<size_t>& indices) {
	indices.resize(v.size());
	std::iota(indices.begin(), indices.end(), 0);
	std::sort(indices.begin(), indices.end(), [&v](size_t i1, size_t i2) {
		return v[i1] > v[i2];
	});

};

};

#define LOG(msg) KTREE::Logger() << msg;




#endif