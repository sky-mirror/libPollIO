cmake_minimum_required(VERSION 3.12)
project(PollIO)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

set(Boost_USE_MULTITHREAD on)

find_package(Boost 1.68.0)
find_package(spdlog REQUIRED)

if(ENABLE_TESTS)
  enable_testing()

  set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/tests/cmake/Modules)

  find_package(GTest REQUIRED)
  find_package(GMock REQUIRED)
  include_directories(tests)
  file(GLOB_RECURSE Test_SRCS tests/*.hpp tests/*.cxx)
  add_executable(unittest ${Test_SRCS})
  target_link_libraries(unittest
    PollIO
    ${GTEST_LIBRARIES} ${GMOCK_LIBRARIES})
endif()



add_library(PollIO INTERFACE)
target_include_directories(PollIO INTERFACE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(PollIO INTERFACE spdlog::spdlog)
