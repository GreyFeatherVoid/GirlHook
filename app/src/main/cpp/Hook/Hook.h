//
// Created by Lynnette on 2025/6/18.
//

#ifndef GIRLHOOK_HOOK_H
#define GIRLHOOK_HOOK_H

#include "../Utility/GirlLog.h"
#include <jni.h>
#include <android/log.h>
#include <unistd.h>
#include <cstring>
#include <dlfcn.h>
#include "../JVM/JVM.h"
#include "../Utility/FindClass.h"
#include "../GlobalStore/GlobalStore.h"
#include <cstdint>
#include <cstddef>
#include <string>
#include <sstream>
#include <iomanip>
#include <utility>
#include <type_traits>
#include "../Bridge/bridge.h"

struct hooked_function{
    std::string org_fullname;
    bool Valid;
    bool isStatic;
    void* ArtMethod;
    void* ourTrampoline;
    uint64_t orgEntryPointPtr = NULL;
    uint64_t orgJNIEntry = NULL;
    uint32_t orgFlag;
    ArtMethodSpec Layout;
    std::string classname;
    std::string name;
    std::string shorty;
    uint64_t backup[128];
    jmethodID methodID;
    std::string enterfuncname;
    std::string leavefuncname;
    std::string script;
};


int hook_java_method(
        const char* org_fullname,
        const char* className,
                      const char* methodName,
                      const char* target_shorty,
                      bool isStatic,
                      const char* enterfuncname,
                      const char* leavefuncname,
                      const char* script);
void unhook(uint32_t hookIndex);
void unhook_all();
void testHook(JNIEnv* env);
#endif //GIRLHOOK_HOOK_H
