#include "RequestHandler.h"
#include <sstream>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <netdb.h>
#include <unistd.h>
#include <string>
#include "MessageForwarder.h"


pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; 

RequestHandler::RequestHandler(std::shared_ptr<CacheManager> cache, std::shared_ptr<Logger> logger)
    : cacheManager(cache), logger(logger), httpParser(std::make_unique<HttpParser>()) {}

void RequestHandler::handleRequest(const std::string& request, int clientSocket, int clientId) {
    try {
        // Parse the http request
        HttpRequest parsedRequest = httpParser->parseRequest(request);
        if (!httpParser->isValidRequest(parsedRequest)) {
            //TODO: fix the format  it should be id: [TYPE] message rather than [TYPE] id:xxxxx
            logger->log(Logger::ERROR, std::to_string(clientId) + ":Invalid request received");
            return ;
        }
        // Build the cache keys
        std::string cacheKey = parsedRequest.method + " " + parsedRequest.url;
        std::string cachedResponse;
        //TODO: test cache later Feb-22
        /*
        if (cacheManager->get(cacheKey, cachedResponse)) {
            logger->log(Logger::INFO, "Cache hit for: " + parsedRequest.url);
            return cachedResponse;
        }
        */
        forwardRequest(parsedRequest, clientSocket,clientId);
        // TODO: test cache later 
        // cacheManager->put(cacheKey, response, time(nullptr) + 300); // Cache for 5 minutes
        
        return;
    } catch (const std::exception& e) {
        logger->log(Logger::ERROR, std::string("Error handling request: ") + e.what());
        return;
    }
}

void RequestHandler::forwardRequest(HttpRequest httpRequest, int clientSocket, int clientId) {
    try {
        MessageForwarder forwarder;
        std::string serverName = httpRequest.headers["Host"];
        std::string requestLine = httpRequest.method + " " + serverName + " " + httpRequest.version;
        
        // Log the request before forwarding
        logger->log("Requesting \"" + httpRequest.request + "\" from " + serverName, clientId);
        
        std::string response;
        if (httpRequest.method == "GET") {
            forwarder.forwardGet(httpRequest, clientSocket, clientId, logger);
        } else if (httpRequest.method == "POST") {
            forwarder.forwardPost(httpRequest, clientSocket, clientId, logger);
        } else if (httpRequest.method == "CONNECT") {
            forwarder.forwardConnect(httpRequest, clientSocket, clientId, logger);
        } else {
            return;
        }
        
        // Parse the first line of the response to log
        size_t firstLineEnd = response.find("\r\n");
        std::string responseLine = response.substr(0, firstLineEnd);
        // Log the response after receiving
        logger->log("Received \"" + responseLine + "\" from " + serverName, clientId);
        
        return ;
    } catch (const std::exception& e) {
        return ;
    }
}