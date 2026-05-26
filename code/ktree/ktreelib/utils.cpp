#include "utils.hpp"

#include <sys/stat.h>
#include <string>
#include <vector>


// check if a directory exists
bool KTREE::dir_exists(const std::string& path) {
	struct stat info;

	int statrc = stat(path.c_str(), &info);
	if (statrc != 0) {
		return false;
	}
	return (info.st_mode & S_IFDIR) ? true: false;
}

// create a directory
bool KTREE::create_dir(const std::string& path) {
	int rc = mkdir(path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	if (rc != 0) {
		return false;
	}
	return true;
}

#ifdef ENABLE_DEBUG_MACRO
	KTREE::LogLevel KTREE::Logger::log_level = KTREE::LogLevel::DEBUG;
#else
	KTREE::LogLevel KTREE::Logger::log_level = KTREE::LogLevel::INFO;
#endif

KTREE::Logger::Logger(LogLevel _level): level(_level) {
	_buffer << (
			get_log_level() == LogLevel::DEBUG ? "DEBUG" :
			get_log_level() == LogLevel::INFO ? "INFO" :
			get_log_level() == LogLevel::WARNING ? "WARNING" : "ERROR")
			<< ": ";
}

KTREE::Logger::~Logger() {
	if (log_level <= Logger::get_log_level()) {
		_buffer << std::endl;
		fprintf(stderr, "%s", _buffer.str().c_str());
		fflush(stderr);
	}
}


const KTREE::LogLevel& KTREE::Logger::get_log_level() {
	return level;
}






