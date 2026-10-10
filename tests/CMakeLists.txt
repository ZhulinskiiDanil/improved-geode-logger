cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The parts of the mod that don't need Geode, tested on their own
project(improved-logger-tests CXX)

add_executable(text-tests TextTests.cpp ../src/Text.cpp)

enable_testing()
add_test(NAME text COMMAND text-tests)
