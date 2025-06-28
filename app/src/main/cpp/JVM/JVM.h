//
// Created by Lynnette on 2025/6/18.
//

#ifndef GIRLHOOK_JVM_H
#define GIRLHOOK_JVM_H



#include <string>
#include "../Utility/GirlLog.h"
#include <jni.h>
#include "../include/ELFIO/elfio/elfio.hpp"
#include <sys/mman.h>
#include <stdio.h>

#define ANDROID_O 26
#define ANDROID_O2 27
#define ANDROID_P 28
#define ANDROID_Q 29
#define ANDROID_R 30
#define ANDROID_S 31

const char *find_path_from_maps(const char *soname);
std::pair<size_t, size_t> find_info_from_maps(const char *soname);
void *get_address_from_module(const char *module_path, const char *symbol_name);

class JavaEnv {
public:
    JavaEnv();
    ~JavaEnv();

    JNIEnv* get() const;
    JNIEnv* operator->() const;
    JavaVM* getJVM() const;
    bool isNull() const;

private:
    JNIEnv* env = nullptr;
    JavaVM* javaVm = nullptr;
    bool attached = false;

    JavaVM* getJavaVMInternal(); // 内部实现：从 libart 获取 JavaVM*
};


namespace tool{
    //分配一块可以执行的内存
    void* allocate_exec_mem(size_t size);
    // 释放 allocate_exec_mem 分配的内存
    bool free_exec_mem(void* addr, size_t size);

    const char *find_path_from_maps(const char *soname);
    std::pair<size_t, size_t> find_info_from_maps(const char *soname);
    void *get_address_from_module(const char *module_path, const char *symbol_name,bool isFunction = true);
    bool is_in_module(void* ptr, const char* module_name);
}

#endif //GIRLHOOK_JVM_H