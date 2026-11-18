#pragma once

#if defined(__ANDROID__) // Android-совместимая версия
#include <android/log.h>
#define LOG_TAG "mc"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#else // Desktop-совместимая версия
#include <cstdio>
#define LOGI(...) do { std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#define LOGE(...) do { std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)
#define LOGW(...) do { std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#endif
