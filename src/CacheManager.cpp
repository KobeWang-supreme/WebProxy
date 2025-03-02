#include "CacheManager.h"
CacheManager::CacheManager(size_t maxSize, size_t cursize) : maxCacheSize(maxSize), currentSize(cursize) {}

bool CacheManager::isExpired(const CacheEntry& entry) const {
    return entry.expiry < time(nullptr);
}
void CacheManager::evictLRU() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    time_t oldestTimestamp = time(nullptr);
    std::string oldestUrl;
    for (const auto& entry : cache) {
        if (entry.second.timestamp < oldestTimestamp) {
            oldestTimestamp = entry.second.timestamp;
            oldestUrl = entry.first;
        }
    }
    remove(oldestUrl);
}
std::shared_ptr<CacheEntry> CacheManager::get(const std::string& url) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    auto it = cache.find(url);
    if (it != cache.end()) {
        if (isExpired(it->second)) { // check if the entry is expired
            std::cout << url << ": in cache, but expired at" << it->second.expiry << std::endl;
            return nullptr; // Important: do i need to remove the entry? 
        }
        else if (it->second.requiresValidation) { // check if the entry requires validation
            std::cout << url << ": in cache, requires validation" << std::endl;
            return std::make_shared<CacheEntry>(it->second); // TODO: Important: still needs validation ( How to handle this case?)
        }
        else { // valid entry
            std::cout << url << ": in cache, valid" << std::endl;
            return std::make_shared<CacheEntry>(it->second); //
        }
    }
    std::cout << url << ": not in cache" << std::endl; // not in cache
    return nullptr;
    
}

void CacheManager::put(const std::string& url, const std::string& response, 
             const std::chrono::seconds& maxAge, bool requiresValidation) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    if (currentSize + response.size()>= maxCacheSize) {
        evictLRU();
    }
    CacheEntry entry;
    entry.response = response;
    entry.timestamp = time(nullptr);
    entry.expiry = entry.timestamp + maxAge.count();
    entry.requiresValidation = requiresValidation;

    cache[url] = entry;
    currentSize += response.size();
    std::cout << url << ": added to cache" << std::endl;    
    
}

void CacheManager::clear() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    cache.clear();
}
void CacheManager::remove(const std::string& url) {
        std::lock_guard<std::mutex> lock(cacheMutex); // 加锁
        auto it = cache.find(url);
        if (it != cache.end()) {
            currentSize -= it->second.response.size();
            cache.erase(it);
            std::cout << url << ": removed from cache" << std::endl;
        }
    }

size_t CacheManager::size() const {
    return cache.size();
} 