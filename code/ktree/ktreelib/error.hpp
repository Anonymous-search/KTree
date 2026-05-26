#ifndef __ERRORS_HPP__
#define __ERRORS_HPP__

#include <exception>
#include <sstream>

namespace KTREE {

template <typename T>
class InvalidArguments: public std::runtime_error {
public:
	InvalidArguments(const std::string& arg, const T& arg_value): std::runtime_error("Invalid argument") {
		std::ostringstream oss;
		oss << "Invalid argument: " << arg << " = " << arg_value;
		static_cast<std::runtime_error&>(*this) = std::runtime_error(oss.str());
	}
};

class KTreeError: public std::runtime_error {
public:
	KTreeError(const std::string& msg);
};

};

#endif // __ERRORS_HPP__