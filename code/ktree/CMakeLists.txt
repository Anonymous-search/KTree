cmake_minimum_required(VERSION 3.10)

project(KTree VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

option(KTREE_ENABLE_THREADS "Enable the experimental multithreaded index build path" OFF)

if(KTREE_ENABLE_THREADS)
find_package(Threads)
endif()

if(KTREE_ENABLE_THREADS AND THREADS_FOUND)
	message(STATUS "Threads found")
	add_definitions(-DMULTITHREADED_ENABLED)
endif()


set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DENABLE_DEBUG_MACRO")

add_subdirectory(ktreelib)
add_executable(ktree main.cpp)

target_include_directories(ktree PUBLIC
	"${PROJECT_BINARY_DIR}"
	"${PROJECT_SOURCE_DIR}/ktreelib"
)

target_link_libraries(ktree PUBLIC KTree)

if(KTREE_ENABLE_THREADS AND THREADS_FOUND)
target_link_libraries(ktree PUBLIC Threads::Threads)
endif()

configure_file(KTreeConfig.h.in KTreeConfig.h)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ktree PRIVATE -O3 -march=native)
    target_compile_options(KTree PRIVATE -O3 -march=native)
endif()