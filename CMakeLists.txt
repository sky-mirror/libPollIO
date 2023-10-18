cmake_minimum_required(VERSION 3.12)
project(PollIO)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

set(Boost_USE_MULTITHREAD on)

find_package(Boost 1.68.0)
find_package(spdlog REQUIRED)

add_library(PollIO INTERFACE)
target_include_directories(PollIO INTERFACE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(PollIO INTERFACE spdlog::spdlog)
