cmake_minimum_required(VERSION 3.13)

project(Asyncpp)
find_package(Threads REQUIRED)

option(BUILD_TEST "Enable test builds" ON)
option(WITH_ASAN "Enable asan for test builds" ON)

add_library(asyncpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dispatcher.cpp
)
target_link_libraries(asyncpp PRIVATE Threads::Threads)
target_include_directories(asyncpp
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(asyncpp PUBLIC cxx_std_20)

if(BUILD_TEST)
    enable_testing()
    include(GoogleTest)
    if(HUNTER_ENABLED)
        hunter_add_package(GTest)
    endif()
    find_package(GTest REQUIRED)

    add_executable(asyncpp-test
        ${CMAKE_CURRENT_SOURCE_DIR}/test/defer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test/fire_and_forget.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test/generator.cpp
    )
    target_link_libraries(asyncpp-test PRIVATE asyncpp GTest::GTest GTest::Main Threads::Threads)

    if(WITH_ASAN)
        message(STATUS "Building with asan enabled")
        target_compile_options(asyncpp-test PRIVATE -fsanitize=address)
        target_link_libraries(asyncpp-test PRIVATE asan)
    endif()

    gtest_add_tests(TARGET asyncpp-test)
endif()