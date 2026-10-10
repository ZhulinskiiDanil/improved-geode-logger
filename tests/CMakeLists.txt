cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The parts of the mod that don't need Geode, tested on their own
project(improved-logger-tests CXX)

enable_testing()

add_executable(text-tests TextTests.cpp ../src/Text.cpp)
add_test(NAME text COMMAND text-tests)

add_executable(search-tests SearchTests.cpp ../src/Search.cpp ../src/Text.cpp)
add_test(NAME search COMMAND search-tests)

add_executable(logline-tests LogLineTests.cpp ../src/LogLine.cpp)
add_test(NAME logline COMMAND logline-tests)

add_executable(model-tests ModelTests.cpp ../src/Model.cpp ../src/Text.cpp)
add_test(NAME model COMMAND model-tests)
