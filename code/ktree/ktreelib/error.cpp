#include "error.hpp"

namespace KTREE {

KTreeError::KTreeError(const std::string& msg): std::runtime_error(msg) {}

};
