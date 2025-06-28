#include <sys/mman.h>
#include <unistd.h>
#include <linux/memfd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <cstring>
#include <iostream>
#include "Communicate.h"
#include <fstream>
#include <string>
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001
#endif
#include <cstdio>

bool deleteFile(const char* path) {
    int result = remove(path); // 或者使用 unlink(path);
    if (result != 0) {
        perror("delete failed");
        return false;
    }
    return true;
}
bool createEmptyFile(const char* path) {
    FILE* file = fopen(path, "w");
    if (!file) {
        perror("create failed");
        return false;
    }
    fclose(file);
    return true;
}


Communicate& Communicate::getInstance() {
    static Communicate instance;  // C++11 之后线程安全
    return instance;
}
Communicate::Communicate() {
    std::ifstream cmdline("/proc/self/cmdline");
    std::string pkg;
    std::getline(cmdline, pkg, '\0');
    this->pkgname = pkg;
    deleteFile(("/data/data/" + this->pkgname + "/fromso").c_str());
    deleteFile(("/data/data/" + this->pkgname + "/fromloader").c_str());
    createEmptyFile(("/data/data/" + this->pkgname + "/fromso").c_str());
    createEmptyFile(("/data/data/" + this->pkgname + "/fromloader").c_str());
}

Communicate::~Communicate() {

}

void Communicate::add(const std::string& data) {
    //这个函数应该被用来添加一条完整的信息
    std::lock_guard<std::mutex> lock(bufferMutex);
    std::string wrap = "--" + data + "==";
    writeBuffer += wrap ;  // 追加到缓冲区
}

size_t Communicate::write() {
    std::string dataToWrite;

    {
        std::lock_guard<std::mutex> lock(bufferMutex);
        if (writeBuffer.empty()) {
            return 0;
        }
        dataToWrite.swap(writeBuffer); // 清空 writeBuffer
    }

    std::string path = "/data/data/" + this->pkgname + "/fromso";

    // 检查文件是否为空
    FILE* check_file = fopen(path.c_str(), "rb");
    if (!check_file) {
        LOGI("[Write] fopen (check) failed");
        return 0;
    }

    int check_fd = fileno(check_file);
    if (flock(check_fd, LOCK_SH) != 0) {
        LOGI("[Write] flock (check) failed");
        fclose(check_file);
        return 0;
    }

    fseek(check_file, 0, SEEK_END);
    auto file_length = ftell(check_file);
    flock(check_fd, LOCK_UN);
    fclose(check_file);

    if (file_length != 0) {
        LOGI("[Write] Wait to be received.");
        return 0;
    }

    // 写入数据（加写锁）
    FILE* file = fopen(path.c_str(), "wb");
    if (!file) {
        LOGI("[Write] fopen for write failed");
        return 0;
    }

    int fd = fileno(file);
    if (flock(fd, LOCK_EX) != 0) {
        LOGI("[Write] flock LOCK_EX failed");
        fclose(file);
        return 0;
    }

    size_t written = fwrite(dataToWrite.data(), 1, dataToWrite.size(), file);
    fflush(file);              // 立即刷新缓冲区
    fd = fileno(file);
    fsync(fd);                 // 等待磁盘写入（可选）

    flock(fd, LOCK_UN); // 解锁
    fclose(file);
    return written;
}

size_t Communicate::read(char** out) {
    std::string filepath = "/data/data/" + this->pkgname + "/fromloader";
    FILE* file = fopen(filepath.c_str(), "rb");
    if (!file) {
        LOGI("[Read] fopen failed");
        return 0;
    }

    int fd = fileno(file);
    if (flock(fd, LOCK_SH) != 0) {
        LOGI("[Read] flock failed");
        fclose(file);
        return 0;
    }

    fseek(file, 0, SEEK_END);
    long file_length = ftell(file);
    if (file_length <= 0) {
        flock(fd, LOCK_UN);  // 解锁
        fclose(file);
        return 0;
    }

    rewind(file);
    *out = new char[file_length + 1];
    if (!*out) {
        LOGI("[Read] malloc failed");
        flock(fd, LOCK_UN);
        fclose(file);
        return 0;
    }
    memset(*out, 0, file_length + 1);

    size_t readBytes = fread(*out, 1, file_length, file);
    if (readBytes != file_length) {
        LOGI("[Read] Warning: fread incomplete %zu vs %ld", readBytes, file_length);
    }

    flock(fd, LOCK_UN);  // 解锁
    fclose(file);

    // 清空文件内容（注意：加写锁）
    FILE* clear_fp = fopen(filepath.c_str(), "wb");
    if (clear_fp) {
        int clear_fd = fileno(clear_fp);
        if (flock(clear_fd, LOCK_EX) == 0) {
            // 成功加锁再清空
            ftruncate(clear_fd, 0);
            flock(clear_fd, LOCK_UN);
        }
        fclose(clear_fp);
    }

    return readBytes;
}

