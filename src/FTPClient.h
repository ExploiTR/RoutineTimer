#ifndef FTPCLIENT_H
#define FTPCLIENT_H

#include <Arduino.h>
#include <WiFiClient.h>

class FTPClient {
private:
    WiFiClient controlClient;
    WiFiClient dataClient;
    String server;
    int port;
    String username;
    String password;
    
    String readResponse();
    bool parsePassiveMode(String response, int& dataPort);
    
public:
    FTPClient();
    ~FTPClient();
    
    // Configuration
    void setServer(String server, int port = 21);
    void setCredentials(String username, String password);
    
    // Connection management
    bool connect();
    bool login();
    void disconnect();
    
    // Directory operations
    bool changeDirectory(String path);
    
    // File operations
    bool fileExists(String filename);
    bool createFile(String filename, String content);
    bool appendToFile(String filename, String content);
    
    // High-level operations
    bool uploadData(String basePath, String filename, String csvData, bool createHeader = false);
};

#endif
