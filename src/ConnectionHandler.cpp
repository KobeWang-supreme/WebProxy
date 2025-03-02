#include "ConnectionHandler.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdexcept>
#include <iostream>
#include <arpa/inet.h>


ConnectionHandler::ConnectionHandler(std::shared_ptr<RequestHandler> handler, std::shared_ptr<Logger> logger)
    : requestHandler(handler), logger(logger), serverSocket(-1), id(0) {}

ConnectionHandler::~ConnectionHandler() {
    stop();
}
/**
 * @brief: Start listen at the the port for clients' requests
 */
void ConnectionHandler::start(int port) {
    // Create the socket
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        logger->log(Logger::ERROR, "Failed to create socket");
        throw std::runtime_error("Failed to create socket");
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);
    // Bind the socket
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        logger->log(Logger::ERROR, "Failed to bind socket");
        throw std::runtime_error("Failed to bind socket");
    }
    // Listen at the socket 
    if (listen(serverSocket, 10) < 0) {
        logger->log(Logger::ERROR, "Failed to listen on socket");
        throw std::runtime_error("Failed to listen on socket");
    }

    while (true) {
        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        // Block until request come
        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket < 0) {
            logger->log(Logger::ERROR, "Failed to accept connection");
            continue;
        }
        
        // Get client IP address
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
        
        //update id
        ++this->id;
        
        // Store the new request
        std::string ip = std::string(clientIP);
        logger->log("from " + ip, id);
        // Create a new thread and execute handleClient func
        clientThreads.emplace_back(&ConnectionHandler::handleClient, this, clientSocket, id);
    } 
}
/**
 * @brief: Handle user request, and return response
 */
void ConnectionHandler::handleClient(int clientSocket, int clientId){
    const int BUFFER_SIZE = 4096;
    char buffer[BUFFER_SIZE];
    // Read the data
    ssize_t bytesRead = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
    if (bytesRead > 0) {
        // Add the end symbol
        buffer[bytesRead] = '\0';
        // Convert the C style string to the C++ style
        std::string request(buffer);
        // Get the response 
        requestHandler->handleRequest(request, clientSocket, clientId);
    }
    close(clientSocket);
}

void ConnectionHandler::stop() {
    if (serverSocket >= 0) {
        close(serverSocket);
        serverSocket = -1;
    }
    // Wait all thread finished
    for (auto& thread : clientThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    clientThreads.clear();
} 