//
// Created by Lynnette on 2025/6/18.
//
#include "JVM.h"
#include <dlfcn.h>

//some code comes from TInjector

namespace tool{
    //用来动态分配trampoline函数
    void* allocate_exec_mem(size_t size) {
        void* mem = mmap(nullptr, size,
                         PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (mem == MAP_FAILED) {
            perror("mmap");
            return nullptr;
        }
        return mem;
    }
// 释放 allocate_exec_mem 分配的内存
    bool free_exec_mem(void* addr, size_t size) {
        if (addr == nullptr || size == 0) {
            return false;
        }
        if (munmap(addr, size) != 0) {
            perror("munmap");
            return false;
        }
        return true;
    }

    const char *find_path_from_maps(const char *soname) {
        FILE *fp = fopen("/proc/self/maps", "r");
        if (fp == NULL) {
            return nullptr;
        }
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, soname)) {
                char *start = strchr(line, '/');
                char *path = strdup(start);
                path[strlen(path) - 1] = '\0';
                fclose(fp);
                return path;
            }
        }
        fclose(fp);
        return nullptr;
    }

    std::pair<size_t, size_t> find_info_from_maps(const char *soname) {
        FILE *fp = fopen("/proc/self/maps", "r");
        if (fp == NULL) {
            return std::make_pair(0, 0);
        }
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, soname)) {
                char *start = strtok(line, "-");
                char *end = strtok(NULL, " ");
                fclose(fp);
                return std::make_pair((size_t) strtoul(start, NULL, 16),
                                      strtoul(end, NULL, 16) - strtoul(start, NULL, 16));
            }
        }
        fclose(fp);
        return std::make_pair(0, 0);
    }

    void *get_address_from_module(const char *module_path, const char *symbol_name, bool isFunction) {
        ELFIO::elfio elffile;
        std::string name;
        ELFIO::Elf64_Addr value;
        ELFIO::Elf_Xword size;
        unsigned char bind;
        unsigned char type;
        ELFIO::Elf_Half section_index;
        unsigned char other;
        const char *file_name = strrchr(module_path, '/');
        elffile.load(module_path);
        size_t module_base = find_info_from_maps(file_name).first;
        ELFIO::section *s = elffile.sections[".dynsym"];
        size_t offset = 0;
        if (s != nullptr) {
            ELFIO::symbol_section_accessor symbol_accessor(elffile, s);
            for (int i = 0; i < symbol_accessor.get_symbols_num(); ++i) {
                symbol_accessor.get_symbol(i, name, value, size, bind, type, section_index, other);
                if (name.find(symbol_name) != std::string::npos &&
                        ((isFunction && type == ELFIO::STT_FUNC) || (!isFunction))
                        ) {
                    offset = value;
                    break;
                }
            }
        }

        s = elffile.sections[".symtab"];
        if (s != nullptr) {
            ELFIO::symbol_section_accessor symbol_accessor(elffile, s);
            for (int i = 0; i < symbol_accessor.get_symbols_num(); ++i) {
                symbol_accessor.get_symbol(i, name, value, size, bind, type, section_index, other);
                if (name.find(symbol_name) != std::string::npos) {
                    offset = value;
                    break;
                }
            }
        }

        for (const auto &segment : elffile.segments) {
            ELFIO::Elf64_Addr seg_vaddr = segment->get_virtual_address();
            ELFIO::Elf_Xword seg_memsz = segment->get_memory_size();

            ELFIO::Elf64_Addr target_vaddr = module_base + offset;

            if (target_vaddr >= module_base + seg_vaddr &&
                target_vaddr < module_base + seg_vaddr + seg_memsz) {
                return (void*)target_vaddr;
            }
        }

        return nullptr;
    }
    bool is_in_module(void* ptr, const char* module_name) {
        std::ifstream maps("/proc/self/maps");
        std::string line;
        while (std::getline(maps, line)) {
            if (line.find(module_name) != std::string::npos) {
                uintptr_t start, end;
                sscanf(line.c_str(), "%lx-%lx", &start, &end);
                if ((uintptr_t)ptr >= start && (uintptr_t)ptr < end)
                    return true;
            }
        }
        return false;
    }
}

JavaEnv::JavaEnv() {
    javaVm = getJavaVMInternal();
    if (!javaVm) {
        LOGE("JavaVM not found.");
        env = nullptr;
        return;
    }

    jint ret = javaVm->GetEnv((void **) &env, JNI_VERSION_1_6);
    if (ret == JNI_EDETACHED) {
        if (javaVm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            attached = true;
            LOGI("Attached thread to JVM");
        } else {
            LOGE("Failed to attach thread");
            env = nullptr;
        }
    } else if (ret != JNI_OK) {
        LOGE("JNI version not supported");
        env = nullptr;
    }
}

JavaEnv::~JavaEnv() {
    if (attached && javaVm) {
        javaVm->DetachCurrentThread();
        LOGI("Detached thread from JVM");
    }
}

JNIEnv* JavaEnv::get() const {
    return env;
}

JNIEnv* JavaEnv::operator->() const {
    return env;
}

bool JavaEnv::isNull() const {
    return env == nullptr;
}
JavaVM* JavaEnv::getJVM() const{
    return this->javaVm;
}
JavaVM* JavaEnv::getJavaVMInternal() {
    using JNI_GetCreatedJavaVMs_t = jint (*)(JavaVM **, jsize, jsize *);

    auto func = (JNI_GetCreatedJavaVMs_t) tool::get_address_from_module(tool::find_path_from_maps("libart.so"), "GetCreatedJavaVMs");
    if (!func) {
        LOGE("dlsym(JNI_GetCreatedJavaVMs) failed");
        return nullptr;
    }

    JavaVM* vms[1];
    jsize num_vms = 0;
    if (func(vms, 1, &num_vms) != JNI_OK || num_vms == 0) {
        LOGE("GetCreatedJavaVMs failed");
        return nullptr;
    }
    return vms[0];
}

