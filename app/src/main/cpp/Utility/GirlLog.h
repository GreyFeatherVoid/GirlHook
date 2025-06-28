//
// Created by Lynnette on 2025/6/18.
//

#ifndef GIRLHOOK_GIRLLOG_H
#define GIRLHOOK_GIRLLOG_H



#include <android/log.h>
#include <string>
#include <sstream>
#include <iomanip>

#define DEBUG
#ifdef DEBUG
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "GirlHook", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "GirlHook", __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "GirlHook", __VA_ARGS__)
#else
#define LOGI(...) ((void)0)
#define LOGE(...) ((void)0)
#define LOGD(...) ((void)0)
#endif
namespace Logger {
    void hex_dump_log(const void *addr, size_t size, const char *tag = "DUMP");
}

#endif //GIRLHOOK_GIRLLOG_H
