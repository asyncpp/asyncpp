cmake_minimum_required(VERSION 3.15)

project(Asyncpp-tests)

option(ASYNCPP_WITH_ASAN "Enable asan for test builds" ON)
option(ASYNCPP_WITH_TSAN "Enable tsan for test builds" OFF)

enable_testing()
include(GoogleTest)

find_package(Threads REQUIRED)

if(HUNTER_ENABLED)
  hunter_add_package(GTest)
  find_package(GTest CONFIG REQUIRED)
else()
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0)
  if(WIN32)
    set(gtest_force_shared_crt
        ON
        CACHE BOOL "" FORCE)
    set(BUILD_GMOCK
        OFF
        CACHE BOOL "" FORCE)
  endif()
  FetchContent_MakeAvailable(googletest)
endif()

add_executable(
  asyncpp-test
  ${CMAKE_CURRENT_SOURCE_DIR}/async_generator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/channel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/defer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/event.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fiber.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fire_and_forget.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/generator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/launch.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/mutex.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/promise.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ptr_tag.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ref.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scope_guard.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/signal.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/so_compat.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/task.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/thread_pool.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/timer.cpp)

if(NOT ASYNCPP_BUILD_TEST)
  find_package(asyncpp CONFIG REQUIRED)
  target_link_libraries(asyncpp-test PRIVATE asyncpp::asyncpp)
else()
  target_link_libraries(asyncpp-test PRIVATE asyncpp)
endif()

target_link_libraries(asyncpp-test PRIVATE GTest::gtest GTest::gtest_main
                                           Threads::Threads)

if(ASYNCPP_WITH_ASAN)
  message(STATUS "Building with asan enabled")
  if(MSVC)
    target_compile_options(asyncpp-test PRIVATE -fsanitize=address /Zi)
    target_compile_definitions(asyncpp-test PRIVATE _DISABLE_VECTOR_ANNOTATION)
    target_compile_definitions(asyncpp-test PRIVATE _DISABLE_STRING_ANNOTATION)
    target_link_libraries(asyncpp-test PRIVATE libsancov.lib)
  else()
    target_compile_options(asyncpp-test PRIVATE -fsanitize=address)
    target_link_libraries(asyncpp-test PRIVATE asan)
  endif()
endif()

if(ASYNCPP_WITH_TSAN)
  message(STATUS "Building with tsan enabled")
  if(MSVC)
    target_compile_options(asyncpp-test PRIVATE -fsanitize=thread /Zi)
    target_compile_definitions(asyncpp-test PRIVATE _DISABLE_VECTOR_ANNOTATION)
    target_compile_definitions(asyncpp-test PRIVATE _DISABLE_STRING_ANNOTATION)
    target_link_libraries(asyncpp-test PRIVATE libsancov.lib)
  else()
    target_compile_options(asyncpp-test PRIVATE -fsanitize=thread)
    target_link_libraries(asyncpp-test PRIVATE tsan)
  endif()
endif()

gtest_discover_tests(asyncpp-test)
