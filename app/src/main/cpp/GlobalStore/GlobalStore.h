//
// Created by weibo on 2025/6/19.
//

#ifndef GIRLHOOK_GLOBALSTORE_H
#define GIRLHOOK_GLOBALSTORE_H
// VectorStore.h
#pragma once

#include <vector>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

template <typename T>
class VectorStore {
public:
    static VectorStore<T>& Instance() {
        static VectorStore<T> instance;
        return instance;
    }

    void Add(const T& item, size_t index) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (index >= data_.size()) {
            data_.resize(index + 1);  // 自动填充空位
        }
        data_[index] = item;
    }

    std::vector<T> GetAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_; // 返回副本（安全）
    }

    T& Get(size_t index) {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_.at(index);
    }

    size_t Size() {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_.size();
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.clear();
    }
    T CopyByIndex(size_t index) {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_.at(index); // 返回的是副本，线程安全
    }


private:
    std::vector<T> data_;
    std::mutex mutex_;

    VectorStore() = default;
    VectorStore(const VectorStore&) = delete;
    VectorStore& operator=(const VectorStore&) = delete;
};

template <typename T>
class UnorderedStore {
public:
    static UnorderedStore<T>& Instance() {
        static UnorderedStore<T> instance;
        return instance;
    }

    // 尝试添加，如果已存在则不会插入，返回是否成功添加
    bool Add(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto result = data_.insert(item);
        return result.second;  // true if inserted, false if already exists
    }

    // 获取所有内容（副本，线程安全）
    std::vector<T> GetAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<T>(data_.begin(), data_.end());
    }

    // 判断是否存在某项
    bool Contains(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_.find(item) != data_.end();
    }

    size_t Size() {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_.size();
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.clear();
    }

private:
    std::unordered_set<T> data_;
    std::mutex mutex_;

    UnorderedStore() = default;
    UnorderedStore(const UnorderedStore&) = delete;
    UnorderedStore& operator=(const UnorderedStore&) = delete;
};

#include <unordered_map>
#include <mutex>

class HookIdLockManager {
public:
    static HookIdLockManager& Instance() {
        static HookIdLockManager instance;
        return instance;
    }

    // 返回对应hookID的锁的引用
    std::mutex& GetMutex(uint32_t hookID) {
        std::lock_guard<std::mutex> lock(map_mutex_);
        return mutex_map_[hookID]; // 如果没有会自动创建
    }

private:
    std::unordered_map<uint32_t, std::mutex> mutex_map_;
    std::mutex map_mutex_;

    HookIdLockManager() = default;
    HookIdLockManager(const HookIdLockManager&) = delete;
    HookIdLockManager& operator=(const HookIdLockManager&) = delete;
};


#endif //GIRLHOOK_GLOBALSTORE_H
