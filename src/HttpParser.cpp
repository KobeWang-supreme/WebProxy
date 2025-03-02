#include "HttpParser.h"
#include <sstream>
#include <iostream>

HttpParser::HttpParser() {}

HttpRequest HttpParser::parseRequest(const std::string& rawRequest) {
    HttpRequest request;
    request.raw = rawRequest;
    // Bind the stream with request
    std::istringstream stream(rawRequest);
    std::string line;

    // Parse request line
    std::getline(stream, line);
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    request.request = line;
    std::istringstream requestLine(line);
    requestLine >> request.method >> request.url >> request.version;
    if (!request.version.empty() && request.version.back() == '\r') {
        request.version.pop_back();
    }

    // Parse headers
    while (std::getline(stream, line) && line != "\r" && line != "") {
        // Format key : value
        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            // Substr(startPoint, length)
            std::string key = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 2);
            if (!value.empty() && value.back() == '\r') {
                value.pop_back();
            }
            if (key == "Host") {
                size_t portPos = value.find(':');
                if (portPos != std::string::npos) {
                    // If port is specified, extract hostname and port
                    request.host = value.substr(0, portPos);
                    request.port = value.substr(portPos + 1);
                } else {
                    // If no port is specified, use the hostname and default port
                    request.host = value;
                    request.port = "80";
                    if (request.method == "CONNECT") {
                        request.port = "443";
                    }
                }       
            }
            request.headers[key] = value;
        }
    }

    // Parse body if present
    std::stringstream bodyStream;
    while (std::getline(stream, line)) {
        bodyStream << line << "\n";
    }
    request.body = bodyStream.str();
    return request;
}

std::string HttpParser::buildRequest(const HttpRequest& request) {
    std::stringstream ss;
    
    ss << request.method << " " << request.url << " " << request.version << "\r\n";
    
    for (const auto& header : request.headers) {
        ss << header.first << ": " << header.second << "\r\n";
    }
    
    ss << "\r\n" << request.body;
    return ss.str();
}

bool HttpParser::isValidRequest(const HttpRequest& request) {
    // Basic validation
    if (request.method.empty() || request.url.empty() || request.version.empty()) {
        return false;
    }
    
    // Validate method
    if (request.method != "GET" && request.method != "POST" && 
        request.method != "CONNECT") {
        return false;
    }
    
    // Validate HTTP version
    if (request.version != "HTTP/1.0" && request.version != "HTTP/1.1") {
        return false;
    }
    
    return true;
} 