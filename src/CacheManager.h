#pragma once
#include <iostream>
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <memory>

struct CacheEntry {
    std::string response;
    time_t timestamp;
    time_t expiry;
    bool requiresValidation;
};

class CacheManager {
private:
    std::unordered_map<std::string, CacheEntry> cache;
    std::mutex cacheMutex;
    size_t maxCacheSize;
    size_t currentSize;
    bool isExpired(const CacheEntry& entry) const; // check if the entry is expired
    void evictLRU(); // evict the least recently used entry

public:
    CacheManager(size_t maxSize = 1024, size_t cursize = 0);
    std::shared_ptr<CacheEntry> get(const std::string& url);
    void put(const std::string& url, const std::string& response, 
             const std::chrono::seconds& maxAge, bool requiresValidation);
    void remove(const std::string& url);
    void clear();
    

    size_t size() const;
}; 