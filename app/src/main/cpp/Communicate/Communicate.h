//
// Created by Lynnette on 2025/6/24.
//

#ifndef GIRLHOOK_COMMUNICATE_H
#define GIRLHOOK_COMMUNICATE_H
#pragma once

#include <mutex>
#include <string>
#include <functional>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/file.h>  // for flock
#include <unistd.h>    // for fileno

#include "../Utility/GirlLog.h"
class Communicate {
public:
    static Communicate& getInstance(); // 获取全局唯一实例
    Communicate();
    ~Communicate();

    // 写入数据（覆盖模式，不追加）
    size_t write();

    size_t read(char** out);

    void add(const std::string& data); // 添加数据到缓冲区


private:
    std::string pkgname;
    std::string writeBuffer; // 缓冲区
    std::mutex bufferMutex;  // 写入互斥锁

};

#endif //GIRLHOOK_COMMUNICATE_H
