#pragma once

// Этот заголовок используют и клиент (Android), и сервер (десктоп/Linux —
// см. server/). На Android используем штатный logcat, иначе — stdout/stderr,
// чтобы world.hpp/chunk_worker_pool.cpp и т.п. компилировались в обоих мирах
// без изменений.
#if defined(__ANDROID__)
#include <android/log.h>

#define LOG_TAG "mc"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define LOGI(...) do { std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#define LOGE(...) do { std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)
#define LOGW(...) do { std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#endif
