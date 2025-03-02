// Response.h
#ifndef RESPONSE_H
#define RESPONSE_H

#include <string>
#include <map>
#include <sstream>
#include <iostream>
#include <vector>

class Response {
private:
    std::string statusLine;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string entireResponse;
    int statusCode;

public:
    Response() : statusCode(0) {}
    
    // Parse the status line from the response
    void ParseLine(const char* response, int length) {
        std::string responseStr(response, length);
        size_t endOfLine = responseStr.find_first_of("\r\n");
        if (endOfLine != std::string::npos) {
            statusLine = responseStr.substr(0, endOfLine);
            
            // Extract status code
            size_t firstSpace = statusLine.find(' ');
            if (firstSpace != std::string::npos) {
                size_t secondSpace = statusLine.find(' ', firstSpace + 1);
                if (secondSpace != std::string::npos) {
                    std::string codeStr = statusLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
                    try {
                        statusCode = std::stoi(codeStr);
                    } catch (const std::exception& e) {
                        std::cerr << "Error parsing status code: " << e.what() << std::endl;
                        statusCode = 0;
                    }
                }
            }
        }
    }
    
    // Parse headers from the response
    void ParseField(const char* response, int length) {
        std::string responseStr(response, length);
        std::istringstream stream(responseStr);
        std::string line;
        
        // Skip the status line
        std::getline(stream, line);
        
        // Parse headers
        while (std::getline(stream, line) && line != "\r" && line != "") {
            if (line.back() == '\r') {
                line.pop_back();
            }
            
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                std::string key = line.substr(0, colonPos);
                std::string value = line.substr(colonPos + 1);
                
                // Trim leading spaces in value
                size_t valueStart = value.find_first_not_of(' ');
                if (valueStart != std::string::npos) {
                    value = value.substr(valueStart);
                }
                
                headers[key] = value;
            }
        }
        
        // Extract body (everything after the blank line)
        size_t bodyStart = responseStr.find("\r\n\r\n");
        if (bodyStart != std::string::npos) {
            body = responseStr.substr(bodyStart + 4);
        }
    }
    
    // Get the status line
    std::string getLine() const {
        return statusLine;
    }
    
    // Get a specific header
    std::string getHeader(const std::string& key) const {
        auto it = headers.find(key);
        if (it != headers.end()) {
            return it->second;
        }
        return "";
    }
    
    // Get all headers
    const std::map<std::string, std::string>& getHeaders() const {
        return headers;
    }
    
    // Get the body
    std::string getBody() const {
        return body;
    }
    
    // Get the status code
    int getStatusCode() const {
        return statusCode;
    }
    
    // Set the entire response
    void setEntireRes(const std::string& response) {
        entireResponse = response;
    }
    
    // Get the entire response
    std::string getEntireRes() const {
        return entireResponse;
    }
    
    // Check if response is cacheable
    bool isCacheable() const {
        // Check for no-store directive
        for (const auto& header : headers) {
            if (header.first == "Cache-Control" && 
                header.second.find("no-store") != std::string::npos) {
                return false;
            }
        }
        
        // Only cache successful responses
        if (statusCode != 200) {
            return false;
        }
        
        return true;
    }
    
    // Get the expiration time for caching
    int getMaxAge() const {
        auto it = headers.find("Cache-Control");
        if (it != headers.end()) {
            std::string cacheControl = it->second;
            size_t maxAgePos = cacheControl.find("max-age=");
            if (maxAgePos != std::string::npos) {
                size_t valueStart = maxAgePos + 8; // Length of "max-age="
                size_t valueEnd = cacheControl.find_first_of(",; ", valueStart);
                if (valueEnd == std::string::npos) {
                    valueEnd = cacheControl.length();
                }
                try {
                    return std::stoi(cacheControl.substr(valueStart, valueEnd - valueStart));
                } catch (const std::exception& e) {
                    return -1;
                }
            }
        }
        return -1;
    }
};

#endif // RESPONSE_H