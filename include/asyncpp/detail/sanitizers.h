#pragma once

#ifndef ASYNCPP_HAS_ASAN
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ASYNCPP_HAS_ASAN 1
#endif
#elif defined(__SANITIZE_ADDRESS__)
#define ASYNCPP_HAS_ASAN 1
#endif
#endif
#ifndef ASYNCPP_HAS_ASAN
#define ASYNCPP_HAS_ASAN 0
#endif
#if ASYNCPP_HAS_ASAN
#include <sanitizer/asan_interface.h>
#endif

#ifndef ASYNCPP_HAS_TSAN
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define ASYNCPP_HAS_TSAN 1
#endif
#elif defined(__SANITIZE_THREAD__)
#define ASYNCPP_HAS_TSAN 1
#endif
#endif
#ifndef ASYNCPP_HAS_TSAN
#define ASYNCPP_HAS_TSAN 0
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
