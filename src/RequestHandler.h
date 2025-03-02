#pragma once
#include <string>
#include <memory>
#include "HttpParser.h"
#include "CacheManager.h"
#include "Logger.h"

class RequestHandler {
private:
    std::shared_ptr<CacheManager> cacheManager;
    std::shared_ptr<Logger> logger;
    std::unique_ptr<HttpParser> httpParser;

public:
    RequestHandler(std::shared_ptr<CacheManager> cache, std::shared_ptr<Logger> logger);
    void handleRequest(const std::string& request, int clientSocket, int clientId);
    void forwardRequest(HttpRequest httpRequest, int clientSocket, int clientId);
}; 