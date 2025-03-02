#include <string>
#include "Logger.h"
#include "Response.hpp"
#include "HttpParser.h"
#include <fcntl.h> 
#define BUFFER_SIZE 65536
class MessageForwarder {
public:
    void forwardGet(HttpRequest& req, int clientSocket,int clientId, std::shared_ptr<Logger> logger);
    void forwardPost(HttpRequest& req, int clientSocket,int clientId, std::shared_ptr<Logger> logger);
    void forwardConnect(HttpRequest& req, int clientSock, int clientId, std::shared_ptr<Logger> logger);
private:
    void sendErrorResponse(int clientSocket, int statusCode, const std::string& statusText);
    int getKeepAliveConnection(const std::string& host, const std::string& port);
    void saveKeepAliveConnection(const std::string& host, const std::string& port, int socket);
    void removeKeepAliveConnection(const std::string& host, const std::string& port);
    std::string buildForwardRequest(const HttpRequest& req);
    std::map<std::string, int> keepAliveConnections;
    std::mutex keepAliveMutex;
    int connectToServer(const std::string& host, const std::string& port);
};