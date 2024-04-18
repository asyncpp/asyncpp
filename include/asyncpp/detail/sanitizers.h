#pragma once

#ifndef ASYNCPP_HAS_ASAN
#if __has_feature(address_sanitizer)
#define ASYNCPP_HAS_ASAN 1
#else
#define ASYNCPP_HAS_ASAN 0
#endif
#endif
#if ASYNCPP_HAS_ASAN
#include <sanitizer/asan_interface.h>
#endif

#ifndef ASYNCPP_HAS_TSAN
#if __has_feature(thread_sanitizer)
#define ASYNCPP_HAS_TSAN 1
#else
#define ASYNCPP_HAS_TSAN 0
#endif
#endif
#if ASYNCPP_HAS_TSAN
#include <sanitizer/tsan_interface.h>
#endif

#ifndef ASYNCPP_HAS_VALGRIND
#if __has_include(<valgrind/valgrind.h>)
#define ASYNCPP_HAS_VALGRIND 1
#else
#define ASYNCPP_HAS_VALGRIND 0
#endif
#endif
#if ASYNCPP_HAS_VALGRIND
#include <valgrind/valgrind.h>
#endif