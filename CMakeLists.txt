cmake_minimum_required(VERSION 3.15)

project(Asyncpp)
find_package(Threads REQUIRED)

option(ASYNCPP_BUILD_TEST "Enable test builds" ON)
option(ASYNCPP_WITH_ASAN "Enable asan for test builds" ON)

add_library(asyncpp INTERFACE)
target_link_libraries(asyncpp INTERFACE Threads::Threads)
target_include_directories(asyncpp
                           INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(asyncpp INTERFACE cxx_std_20)

# G++ below 11 needs a flag
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "11.0")
    target_compile_options(asyncpp INTERFACE -fcoroutines)
  endif()
endif()

if(ASYNCPP_BUILD_TEST)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1)
  enable_testing()
  include(GoogleTest)
  if(HUNTER_ENABLED)
    hunter_add_package(GTest)
    find_package(GTest CONFIG REQUIRED)
    add_library(GTest::GTest ALIAS GTest::gtest)
    add_library(GTest::Main ALIAS GTest::gtest_main)
  else()
    if(WIN32)
      set(gtest_force_shared_crt
          ON
          CACHE BOOL "" FORCE)
    endif()
    FetchContent_MakeAvailable(googletest)
  endif()

  add_executable(
    asyncpp-test
    ${CMAKE_CURRENT_SOURCE_DIR}/test/defer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/event.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/fire_and_forget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/generator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/launch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/promise.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/ptr_tag.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/ref.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/scope_guard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/task.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/thread_pool.cpp)
  target_link_libraries(asyncpp-test PRIVATE asyncpp GTest::gtest
                                             GTest::gtest_main Threads::Threads)

  if(ASYNCPP_WITH_ASAN)
    message(STATUS "Building with asan enabled")
    if(MSVC)
      target_compile_options(asyncpp-test PRIVATE -fsanitize=address /Zi)
      target_compile_definitions(asyncpp-test
                                 PRIVATE _DISABLE_VECTOR_ANNOTATION)
      target_compile_definitions(asyncpp-test
                                 PRIVATE _DISABLE_STRING_ANNOTATION)
      target_link_libraries(asyncpp-test PRIVATE libsancov.lib)
    else()
      target_compile_options(asyncpp-test PRIVATE -fsanitize=address)
      target_link_libraries(asyncpp-test PRIVATE asan)
    endif()
  endif()

  gtest_discover_tests(asyncpp-test)
endif()
