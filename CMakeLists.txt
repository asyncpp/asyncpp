cmake_minimum_required(VERSION 3.15)

project(Asyncpp)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Threads REQUIRED)

option(ASYNCPP_BUILD_TEST "Enable test builds" ON)
option(ASYNCPP_BUILD_DOCS "Enable test builds" OFF)
option(ASYNCPP_SO_COMPAT "Enable shared object compatibility mode" OFF)

add_library(asyncpp INTERFACE)
target_link_libraries(asyncpp INTERFACE Threads::Threads)
file(GLOB_RECURSE ASYNCPP_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/include/asyncpp/*.h)
target_sources(
  asyncpp
  INTERFACE FILE_SET HEADERS BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
            FILES ${ASYNCPP_SOURCES})
target_compile_features(asyncpp INTERFACE cxx_std_20)

if(ASYNCPP_SO_COMPAT)
  target_compile_definitions(asyncpp INTERFACE ASYNCPP_SO_COMPAT)
endif()

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/asyncpp-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/asyncpp-config.cmake
  INSTALL_DESTINATION lib/cmake/asyncpp
  PATH_VARS CMAKE_INSTALL_INCLUDEDIR)

install(
  TARGETS asyncpp
  EXPORT asyncpp-targets
  FILE_SET HEADERS
  DESTINATION include
  INCLUDES
  DESTINATION include)

install(
  EXPORT asyncpp-targets
  NAMESPACE asyncpp::
  DESTINATION lib/cmake/asyncpp)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/asyncpp-config.cmake
        DESTINATION lib/cmake/asyncpp)

# G++ below 11 needs a flag
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "11.0")
    target_compile_options(asyncpp INTERFACE -fcoroutines)
  endif()
endif()
if(APPLE)
  target_compile_definitions(asyncpp INTERFACE _XOPEN_SOURCE=600)
endif()

if(ASYNCPP_BUILD_TEST)
  add_subdirectory(test)
endif()

if(ASYNCPP_BUILD_DOCS)
  add_subdirectory(docs)
endif()
