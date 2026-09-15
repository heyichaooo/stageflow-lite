cmake_minimum_required(VERSION 3.14)
project(stageflow-lite VERSION 0.1.0 LANGUAGES CXX)

add_library(stageflow INTERFACE)
add_library(stageflow::stageflow ALIAS stageflow)
target_include_directories(stageflow INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(stageflow INTERFACE cxx_std_17)

option(STAGEFLOW_BUILD_EXAMPLES "Build stageflow examples" ON)
option(STAGEFLOW_BUILD_TESTS "Build stageflow tests" ON)

if (STAGEFLOW_BUILD_EXAMPLES)
    add_executable(stageflow_basic_example examples/basic.cpp)
    target_link_libraries(stageflow_basic_example PRIVATE stageflow::stageflow)
endif()

if (STAGEFLOW_BUILD_TESTS)
    enable_testing()
    add_executable(stageflow_tests tests/stageflow_test.cpp)
    target_link_libraries(stageflow_tests PRIVATE stageflow::stageflow)
    add_test(NAME stageflow_tests COMMAND stageflow_tests)
endif()
