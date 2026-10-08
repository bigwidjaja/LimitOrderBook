cmake_minimum_required(VERSION 3.20)
project(orderbook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ORDERBOOK_SANITIZE "Build with address + UB sanitizers" ON)
if(ORDERBOOK_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(orderbook_core src/orderbook.cpp)
target_include_directories(orderbook_core PUBLIC include)
target_compile_options(orderbook_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

include(FetchContent)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.7.1)
FetchContent_MakeAvailable(Catch2)

add_executable(orderbook_tests tests/orderbook_test.cpp)
target_compile_options(orderbook_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(orderbook_tests PRIVATE orderbook_core Catch2::Catch2WithMain)

enable_testing()
add_test(NAME orderbook_tests COMMAND orderbook_tests)
