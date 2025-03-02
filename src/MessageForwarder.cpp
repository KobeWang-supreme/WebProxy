#include "MessageForwarder.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <iostream>
#include <algorithm>

void MessageForwarder::forwardGet(HttpRequest& req, int clientSocket, int clientId, std::shared_ptr<Logger> logger) {
    // Log the request before forwarding
    logger->log("Requesting \"" + req.request + " from " + req.host, clientId);
    // Connect to the target server
    int serverSocket = connectToServer(req.host, req.port);
    if (serverSocket < 0) {
        logger->log(Logger::LogLevel::ERROR, "Failed to connect to server: " + req.host + ":" + req.port);
        sendErrorResponse(clientSocket, 502, "Bad Gateway");
        return;
    }
    
    // Forward the request to the server
    std::string requestToSend = buildForwardRequest(req);
    if (send(serverSocket, requestToSend.c_str(), requestToSend.length(), 0) < 0) {
        logger->log(Logger::LogLevel::ERROR, "Failed to send request to server");
        close(serverSocket);
        sendErrorResponse(clientSocket, 500, "Internal Server Error");
        return;
    }
    
    // Read and forward the response from the server to the client
    bool keepAliveServer = false;
    bool keepAliveClient = false;
    
    // Check if client requested keep-alive
    auto clientConnectionIt = req.headers.find("Connection");
    if (clientConnectionIt != req.headers.end() && 
        strcasecmp(clientConnectionIt->second.c_str(), "keep-alive") == 0) {
        keepAliveClient = true;
    }
    
    // Buffer for receiving data
    char buffer[BUFFER_SIZE];
    ssize_t bytesRead;
    std::string responseHeaders;
    bool headersComplete = false;
    size_t contentLength = 0;
    size_t receivedBodyBytes = 0;
    bool chunkedEncoding = false;
    
    // Read and process the response
    while ((bytesRead = recv(serverSocket, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytesRead] = '\0';  // Null-terminate for string operations
        
        // If we haven't finished reading headers yet
        if (!headersComplete) {
            responseHeaders.append(buffer, bytesRead);
            
            // Check if we've received all headers
            size_t headerEnd = responseHeaders.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                headersComplete = true;
                
                // Extract headers to check for keep-alive and content length
                std::string headerSection = responseHeaders.substr(0, headerEnd);
                
                // Check if server supports keep-alive
                if (headerSection.find("Connection: keep-alive") != std::string::npos) {
                    keepAliveServer = true;
                }
                
                // Check for Content-Length
                size_t contentLengthPos = headerSection.find("Content-Length: ");
                if (contentLengthPos != std::string::npos) {
                    size_t valueStart = contentLengthPos + 16; // Length of "Content-Length: "
                    size_t valueEnd = headerSection.find("\r\n", valueStart);
                    std::string lengthStr = headerSection.substr(valueStart, valueEnd - valueStart);
                    contentLength = std::stoul(lengthStr);
                }
                
                // Check for chunked encoding
                if (headerSection.find("Transfer-Encoding: chunked") != std::string::npos) {
                    chunkedEncoding = true;
                }
                
                // Calculate how much of the body we've already received
                receivedBodyBytes = responseHeaders.length() - (headerEnd + 4); // +4 for \r\n\r\n
                
                // Send the complete headers and any part of the body we've received to the client
                if (send(clientSocket, responseHeaders.c_str(), responseHeaders.length(), 0) < 0) {
                    // logger->log(LogLevel::ERROR, "Failed to send response headers to client");
                    break;
                }
                
                // If there's no body or we've already received the complete body
                if ((contentLength > 0 && receivedBodyBytes >= contentLength) || 
                    (contentLength == 0 && !chunkedEncoding)) {
                    break;
                }
            }
        } else {
            // We've already sent the headers, now just forward the body data directly
            if (send(clientSocket, buffer, bytesRead, 0) < 0) {
                // logger->log(LogLevel::ERROR, "Failed to send response body to client");
                break;
            }
            
            receivedBodyBytes += bytesRead;
            
            // If we know the content length and we've received all data, exit the loop
            if (contentLength > 0 && receivedBodyBytes >= contentLength) {
                break;
            }
            
            // For chunked encoding, look for the end chunk marker "0\r\n\r\n"
            if (chunkedEncoding) {
                std::string chunk(buffer, bytesRead);
                if (chunk.find("0\r\n\r\n") != std::string::npos) {
                    break;
                }
            }
        }
    }
    
    //Handle read errors or connection closed by server
    if (bytesRead < 0) {
        logger->log(Logger::LogLevel::ERROR, "Error reading response from server: " + std::string(strerror(errno)));
    }
    
    //Close the server connection if keep-alive is not supported/requested
    if (!keepAliveServer) {
        close(serverSocket);
    } else {
        // Store the connection for future use
        saveKeepAliveConnection(req.host, req.port, serverSocket);
    }
    
    logger->log(Logger::LogLevel::INFO, "Completed forwarding GET request for client " + std::to_string(clientId));
}

// Helper function to build the forwarded request
std::string MessageForwarder::buildForwardRequest(const HttpRequest& req) {
    std::stringstream ss;
    
    // Build the request line
    ss << req.method << " " << req.request << " " << req.version << "\r\n";
    
    // Add headers
    for (const auto& header : req.headers) {
        // Skip hop-by-hop headers
        if (strcasecmp(header.first.c_str(), "Connection") == 0 ||
            strcasecmp(header.first.c_str(), "Keep-Alive") == 0 ||
            strcasecmp(header.first.c_str(), "Proxy-Connection") == 0 ||
            strcasecmp(header.first.c_str(), "Proxy-Authorization") == 0 ||
            strcasecmp(header.first.c_str(), "TE") == 0 ||
            strcasecmp(header.first.c_str(), "Trailer") == 0 ||
            strcasecmp(header.first.c_str(), "Transfer-Encoding") == 0 ||
            strcasecmp(header.first.c_str(), "Upgrade") == 0) {
            continue;
        }
        ss << header.first << ": " << header.second << "\r\n";
    }
    
    // Add our own Connection header if needed
    ss << "Connection: keep-alive\r\n";
    
    // End of headers
    ss << "\r\n";
    
    return ss.str();
}

/*
@brief: Helper function to connect to the target server
*/
 int MessageForwarder::connectToServer(const std::string& host, const std::string& port) {
    // First check if we already have a keep-alive connection
    int existingSocket = getKeepAliveConnection(host, port);
    if (existingSocket > 0) {
        // Test if the connection is still valid
        char test;
        if (recv(existingSocket, &test, 1, MSG_PEEK | MSG_DONTWAIT) == 0) {
            close(existingSocket);
            removeKeepAliveConnection(host, port);
        } else {
            return existingSocket;
        }
    }
    
    // Create a new connection
    struct addrinfo hints, *res;
    int sockfd;
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0) {
        return -1;
    }
    
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        freeaddrinfo(res);
        return -1;
    }
    
    // Non-blocking mode
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    
    // Attempt to connect
    int connectResult = connect(sockfd, res->ai_addr, res->ai_addrlen);
    if (connectResult < 0) {
        if (errno == EINPROGRESS) {
            fd_set fdset;
            struct timeval tv;
            
            FD_ZERO(&fdset);
            FD_SET(sockfd, &fdset);
            // 5 s Time out
            tv.tv_sec = 5;
            tv.tv_usec = 0;
            if (select(sockfd + 1, NULL, &fdset, NULL, &tv) == 1) {
                int so_error;
                socklen_t len = sizeof so_error;
                getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len);
                
                if (so_error != 0) {
                    close(sockfd);
                    freeaddrinfo(res);
                    return -1;
                }
            } else {
                // Error
                close(sockfd);
                freeaddrinfo(res);
                return -1;
            }
        } else {
            close(sockfd);
            freeaddrinfo(res);
            return -1;
        }
    }
    // Blocking mode
    fcntl(sockfd, F_SETFL, flags);
    freeaddrinfo(res);
    return sockfd;
}

/*
 @brief: function to send an error response to the client
*/
 void MessageForwarder::sendErrorResponse(int clientSocket, int statusCode, const std::string& statusText) {
    std::stringstream ss;
    ss << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
    ss << "Content-Type: text/html\r\n";
    ss << "Connection: close\r\n";
    
    std::string body = "<html><body><h1>" + std::to_string(statusCode) + " " + statusText + "</h1></body></html>";
    
    ss << "Content-Length: " << body.length() << "\r\n";
    ss << "\r\n";
    ss << body;
    
    std::string response = ss.str();
    send(clientSocket, response.c_str(), response.length(), 0);
}

/*
@ brief: Helper function to get a keep-alive connection
*/
int MessageForwarder::getKeepAliveConnection(const std::string& host, const std::string& port) {
    std::string key = host + ":" + port;
    std::lock_guard<std::mutex> guard(keepAliveMutex); 
    auto it = keepAliveConnections.find(key);
    if (it != keepAliveConnections.end()) {
        return it->second;
    }
    return -1;
}

void MessageForwarder::saveKeepAliveConnection(const std::string& host, const std::string& port, int socket) {
    std::string key = host + ":" + port;
    std::lock_guard<std::mutex> guard(keepAliveMutex);
    
    // If there was an old connection, close it first
    auto it = keepAliveConnections.find(key);
    if (it != keepAliveConnections.end()) {
        close(it->second);
    }
    
    keepAliveConnections[key] = socket;
}

void MessageForwarder::removeKeepAliveConnection(const std::string& host, const std::string& port) {
    std::string key = host + ":" + port;
    std::lock_guard<std::mutex> guard(keepAliveMutex);
    
    keepAliveConnections.erase(key);
}

void MessageForwarder::forwardPost(HttpRequest& req, int clientSocket, int clientId, std::shared_ptr<Logger> logger) {
    logger->log(Logger::LogLevel::INFO, "Forwarding POST request for client " + std::to_string(clientId) + ": " + req.url);
    
    //Connect to the target server
    std::string port = req.port.empty() ? "80" : req.port;
    int serverSocket = connectToServer(req.host, port);
    
    if (serverSocket < 0) {
        logger->log(Logger::LogLevel::ERROR, "Failed to connect to server: " + req.host + ":" + port);
        sendErrorResponse(clientSocket, 502, "Bad Gateway");
        return;
    }
    
    //Check Content-Length header
    size_t contentLength = 0;
    auto contentLengthIt = req.headers.find("Content-Length");
    if (contentLengthIt != req.headers.end()) {
        try {
            contentLength = std::stoul(contentLengthIt->second);
        } catch (const std::exception& e) {
            logger->log(Logger::LogLevel::ERROR, "Invalid Content-Length: " + contentLengthIt->second);
            close(serverSocket);
            sendErrorResponse(clientSocket, 400, "Bad Request");
            return;
        }
    }
    
    // Check for chunked encoding
    bool chunkedEncoding = false;
    auto transferEncodingIt = req.headers.find("Transfer-Encoding");
    if (transferEncodingIt != req.headers.end() && 
        transferEncodingIt->second.find("chunked") != std::string::npos) {
        chunkedEncoding = true;
    }
    
    // If don't have Content-Length don't have chunked encoding 
    if (contentLength == 0 && !chunkedEncoding && !req.body.empty()) {
        logger->log(Logger::LogLevel::ERROR, "POST request without proper Content-Length or Transfer-Encoding");
        close(serverSocket);
        sendErrorResponse(clientSocket, 400, "Bad Request");
        return;
    }
    
    // Build the request to forward
    std::string requestToSend = buildForwardRequest(req);
    
    // For POST requests, we need to append the body
    requestToSend += req.body;
    
    // Send the request to the server
    if (send(serverSocket, requestToSend.c_str(), requestToSend.length(), 0) < 0) {
        logger->log(Logger::LogLevel::ERROR, "Failed to send POST request to server: " + std::string(strerror(errno)));
        close(serverSocket);
        sendErrorResponse(clientSocket, 500, "Internal Server Error");
        return;
    }
    
    if (chunkedEncoding && req.body.find("0\r\n\r\n") == std::string::npos) {
        logger->log(Logger::LogLevel::DEBUG, "Reading additional chunked data from client");
        
        char buffer[BUFFER_SIZE];
        bool chunkedComplete = false;
        
        while (!chunkedComplete) {
            ssize_t bytesRead = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
            
            if (bytesRead <= 0) {
                if (bytesRead < 0) {
                    logger->log(Logger::LogLevel::ERROR, "Error reading chunked data from client: " + std::string(strerror(errno)));
                } else {
                    logger->log(Logger::LogLevel::ERROR, "Client closed connection while reading chunked data");
                }
                close(serverSocket);
                return;
            }
            
            buffer[bytesRead] = '\0';
            std::string chunk(buffer, bytesRead);
            
            //Forward the chunk to the server
            if (send(serverSocket, chunk.c_str(), chunk.length(), 0) < 0) {
                logger->log(Logger::LogLevel::ERROR, "Failed to forward chunk to server: " + std::string(strerror(errno)));
                close(serverSocket);
                return;
            }
            
            //Check if this is the last chunk
            if (chunk.find("0\r\n\r\n") != std::string::npos) {
                chunkedComplete = true;
            }
        }
    }
    
    //Read and forward the response from the server to the client
    bool keepAliveServer = false;
    bool keepAliveClient = false;
    
    //Check if client requested keep-alive
    auto clientConnectionIt = req.headers.find("Connection");
    if (clientConnectionIt != req.headers.end() && 
        strcasecmp(clientConnectionIt->second.c_str(), "keep-alive") == 0) {
        keepAliveClient = true;
    }
    
    //Process server response
    char buffer[BUFFER_SIZE];
    ssize_t bytesRead;
    std::string responseHeaders;
    bool headersComplete = false;
    size_t responseContentLength = 0;
    size_t receivedBodyBytes = 0;
    bool responseChunked = false;
    
    //Read and process the response
    while ((bytesRead = recv(serverSocket, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytesRead] = '\0';
        
        if (!headersComplete) {
            responseHeaders.append(buffer, bytesRead);
            
            size_t headerEnd = responseHeaders.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                headersComplete = true;
                
                //Extract headers to check for keep-alive and content length
                std::string headerSection = responseHeaders.substr(0, headerEnd);
                
                //Check if server supports keep-alive
                if (headerSection.find("Connection: keep-alive") != std::string::npos) {
                    keepAliveServer = true;
                }
                
                //Check for Content-Length
                size_t contentLengthPos = headerSection.find("Content-Length: ");
                if (contentLengthPos != std::string::npos) {
                    size_t valueStart = contentLengthPos + 16; // Length of "Content-Length: "
                    size_t valueEnd = headerSection.find("\r\n", valueStart);
                    std::string lengthStr = headerSection.substr(valueStart, valueEnd - valueStart);
                    responseContentLength = std::stoul(lengthStr);
                }
                
                //Check for chunked encoding
                if (headerSection.find("Transfer-Encoding: chunked") != std::string::npos) {
                    responseChunked = true;
                }
                
                //Calculate how much of the body we've already received
                receivedBodyBytes = responseHeaders.length() - (headerEnd + 4); 
                
                //Send the complete headers and any part of the body we've received to the client
                if (send(clientSocket, responseHeaders.c_str(), responseHeaders.length(), 0) < 0) {
                    logger->log(Logger::LogLevel::ERROR, "Failed to send response headers to client");
                    break;
                }
                
                if ((responseContentLength > 0 && receivedBodyBytes >= responseContentLength) || 
                    (responseContentLength == 0 && !responseChunked)) {
                    break;
                }
            }
        } else {
            if (send(clientSocket, buffer, bytesRead, 0) < 0) {
                logger->log(Logger::LogLevel::ERROR, "Failed to send response body to client");
                break;
            }
            
            receivedBodyBytes += bytesRead;
        
            if (responseContentLength > 0 && receivedBodyBytes >= responseContentLength) {
                break;
            }
            
            if (responseChunked) {
                std::string chunk(buffer, bytesRead);
                if (chunk.find("0\r\n\r\n") != std::string::npos) {
                    break;
                }
            }
        }
    }
    
    //Handle read errors or connection closed by server
    if (bytesRead < 0) {
        logger->log(Logger::LogLevel::ERROR, "Error reading response from server: " + std::string(strerror(errno)));
    }
    
    //Close the server connection if keep-alive is not supported/requested
    if (!keepAliveServer) {
        close(serverSocket);
    } else {
        // Store the connection for future use
        saveKeepAliveConnection(req.host, port, serverSocket);
    }
    
    logger->log(Logger::LogLevel::INFO, "Completed forwarding POST request for client " + std::to_string(clientId));
}
    
void MessageForwarder::forwardConnect(HttpRequest& req, int clientSocket, int clientId, std::shared_ptr<Logger> logger) {
    logger->log(Logger::INFO, "Handling CONNECT request for client " + std::to_string(clientId) + ": " + req.host + ":" + req.port);
    
    //Connect to the target server
    int serverSocket = connectToServer(req.host, req.port);
    if (serverSocket < 0) {
        logger->log(Logger::ERROR, "Failed to connect to server: " + req.host + ":" + req.port);
        sendErrorResponse(clientSocket, 502, "Bad Gateway");
        return;
    }
    
    //Send 200 Connection Established response to the client
    std::string response = "HTTP/1.1 200 Connection Established\r\n";
    response += "Proxy-Agent: MyProxy/1.0\r\n";
    response += "\r\n";
    
    if (send(clientSocket, response.c_str(), response.length(), 0) < 0) {
        logger->log(Logger::ERROR, "Failed to send Connection Established response to client");
        close(serverSocket);
        return;
    }
    
    //Set up for tunneling data between client and server
    fd_set readFds;
    char buffer[BUFFER_SIZE];
    bool tunnelActive = true;
    
    //Make both sockets non-blocking for the tunneling
    int clientFlags = fcntl(clientSocket, F_GETFL, 0);
    int serverFlags = fcntl(serverSocket, F_GETFL, 0);
    fcntl(clientSocket, F_SETFL, clientFlags | O_NONBLOCK);
    fcntl(serverSocket, F_SETFL, serverFlags | O_NONBLOCK);
    
    logger->log(Logger::LogLevel::INFO, "Established tunnel for client " + std::to_string(clientId) + " to " + req.host + ":" + req.port);
    
    // Tunnel Loop
    while (tunnelActive) {
        FD_ZERO(&readFds);
        FD_SET(clientSocket, &readFds);
        FD_SET(serverSocket, &readFds);
        
        int maxFd = std::max(clientSocket, serverSocket) + 1;
        struct timeval timeout;
        timeout.tv_sec = 30; 
        timeout.tv_usec = 0;
        
        int activity = select(maxFd, &readFds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            if (errno == EINTR) {
                continue;
            }
            logger->log(Logger::LogLevel::ERROR, "Select error in tunnel: " + std::string(strerror(errno)));
            break;
        }
        
        if (activity == 0) {
            logger->log(Logger::LogLevel::DEBUG, "Tunnel timeout for client " + std::to_string(clientId) + ", checking connection");
            // Send a TCP keep-alive packet (optional, depends on implementation)
            // If no activity for this long, we could just close the tunnel
            continue;
        }
        
        // Check if client has data to read
        if (FD_ISSET(clientSocket, &readFds)) {
            ssize_t bytesRead = recv(clientSocket, buffer, BUFFER_SIZE, 0);
            
            if (bytesRead <= 0) {
                // Client closed connection or error
                if (bytesRead < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    // Non-blocking operation would block, try again later
                    continue;
                }
                
                logger->log(Logger::LogLevel::INFO, "Client " + std::to_string(clientId) + " closed connection or error occurred");
                tunnelActive = false;
                break;
            }
            
            // Forward data to server
            ssize_t bytesSent = 0;
            ssize_t totalBytesSent = 0;
            
            while (totalBytesSent < bytesRead) {
                bytesSent = send(serverSocket, buffer + totalBytesSent, bytesRead - totalBytesSent, 0);
                
                if (bytesSent <= 0) {
                    if (bytesSent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        // Would block, try again with select
                        fd_set writeFds;
                        FD_ZERO(&writeFds);
                        FD_SET(serverSocket, &writeFds);
                        
                        timeout.tv_sec = 5;  // 5 seconds timeout for write
                        timeout.tv_usec = 0;
                        
                        if (select(serverSocket + 1, NULL, &writeFds, NULL, &timeout) <= 0) {
                            // Timeout or error
                            tunnelActive = false;
                            break;
                        }
                        continue;
                    }
                    
                    // Error occurred
                    logger->log(Logger::LogLevel::ERROR, "Error sending data to server: " + std::string(strerror(errno)));
                    tunnelActive = false;
                    break;
                }
                
                totalBytesSent += bytesSent;
            }
            
            if (!tunnelActive) {
                break;
            }
        }
        
        // Check if server has data to read
        if (FD_ISSET(serverSocket, &readFds)) {
            ssize_t bytesRead = recv(serverSocket, buffer, BUFFER_SIZE, 0);
            
            if (bytesRead <= 0) {
                // Server closed connection or error
                if (bytesRead < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    // Non-blocking operation would block, try again later
                    continue;
                }
                
                logger->log(Logger::LogLevel::INFO, "Server closed connection or error occurred");
                tunnelActive = false;
                break;
            }
            
            // Forward data to client
            ssize_t bytesSent = 0;
            ssize_t totalBytesSent = 0;
            
            while (totalBytesSent < bytesRead) {
                bytesSent = send(clientSocket, buffer + totalBytesSent, bytesRead - totalBytesSent, 0);
                
                if (bytesSent <= 0) {
                    if (bytesSent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        // Would block, try again with select
                        fd_set writeFds;
                        FD_ZERO(&writeFds);
                        FD_SET(clientSocket, &writeFds);
                        
                        timeout.tv_sec = 5;  // 5 seconds timeout for write
                        timeout.tv_usec = 0;
                        
                        if (select(clientSocket + 1, NULL, &writeFds, NULL, &timeout) <= 0) {
                            // Timeout or error
                            tunnelActive = false;
                            break;
                        }
                        continue;
                    }
                    
                    // Error occurred
                    logger->log(Logger::LogLevel::ERROR, "Error sending data to client: " + std::string(strerror(errno)));
                    tunnelActive = false;
                    break;
                }
                
                totalBytesSent += bytesSent;
            }
            
            if (!tunnelActive) {
                break;
            }
        }
    }
    
    // Clean up
    close(serverSocket);
    logger->log(Logger::LogLevel::INFO, "Closed tunnel for client " + std::to_string(clientId) + " to " + req.host + ":" + req.port);
    
    // Note: The client socket is not closed here as it's managed by the caller
}
<<<<<<< HEAD
=======

>>>>>>> 412e57a72e1b369e9c713e5d0222099445729b29
