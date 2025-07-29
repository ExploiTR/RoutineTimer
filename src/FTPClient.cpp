#include "FTPClient.h"

FTPClient::FTPClient() {
    port = 21;
}

FTPClient::~FTPClient() {
    disconnect();
}

void FTPClient::setServer(String server, int port) {
    this->server = server;
    this->port = port;
}

void FTPClient::setCredentials(String username, String password) {
    this->username = username;
    this->password = password;
}

String FTPClient::readResponse() {
    String response = "";
    unsigned long timeout = millis() + 5000; // 5 second timeout
    
    while (millis() < timeout) {
        if (controlClient.available()) {
            response += controlClient.readString();
            break;
        }
        delay(10);
    }
    
    if (response.length() > 0) {
        Serial.print("FTP Response: " + response);
    }
    return response;
}

bool FTPClient::parsePassiveMode(String response, int& dataPort) {
    // Parse passive mode response to get data connection details
    int start = response.indexOf('(');
    int end = response.indexOf(')', start);
    
    if (start == -1 || end == -1) {
        Serial.println("Invalid passive mode response format");
        return false;
    }
    
    String dataInfo = response.substring(start + 1, end);
    
    // Extract IP and port from passive mode response
    // Format: (192,168,1,1,20,40) where port = 20*256 + 40
    int commas[5];
    int commaCount = 0;
    for (int i = 0; i < dataInfo.length() && commaCount < 5; i++) {
        if (dataInfo.charAt(i) == ',') {
            commas[commaCount++] = i;
        }
    }
    
    if (commaCount == 5) {
        int p1 = dataInfo.substring(commas[3] + 1, commas[4]).toInt();
        int p2 = dataInfo.substring(commas[4] + 1).toInt();
        dataPort = p1 * 256 + p2;
        return true;
    }
    
    Serial.println("Failed to parse passive mode response");
    return false;
}

bool FTPClient::connect() {
    Serial.printf("Connecting to FTP server %s:%d\n", server.c_str(), port);
    
    if (!controlClient.connect(server.c_str(), port)) {
        Serial.println("FTP connection failed!");
        return false;
    }
    
    String response = readResponse();
    if (response.startsWith("220")) {
        Serial.println("FTP server connected successfully");
        return true;
    }
    
    Serial.println("FTP server connection failed - invalid response");
    return false;
}

bool FTPClient::login() {
    Serial.println("Logging into FTP server...");
    
    // Send username
    controlClient.printf("USER %s\r\n", username.c_str());
    String response = readResponse();
    
    if (!response.startsWith("331") && !response.startsWith("230")) {
        Serial.println("FTP login failed - username rejected");
        return false;
    }
    
    // Send password
    controlClient.printf("PASS %s\r\n", password.c_str());
    response = readResponse();
    
    if (response.startsWith("230")) {
        Serial.println("FTP login successful");
        return true;
    }
    
    Serial.println("FTP login failed - password rejected");
    return false;
}

void FTPClient::disconnect() {
    if (controlClient.connected()) {
        controlClient.printf("QUIT\r\n");
        readResponse();
        controlClient.stop();
    }
    
    if (dataClient.connected()) {
        dataClient.stop();
    }
    
    Serial.println("FTP disconnected");
}

bool FTPClient::changeDirectory(String path) {
    Serial.printf("Changing to directory: %s\n", path.c_str());
    
    controlClient.printf("CWD %s\r\n", path.c_str());
    String response = readResponse();
    
    if (response.startsWith("250")) {
        Serial.println("Directory changed successfully");
        return true;
    }
    
    Serial.println("Failed to change directory");
    return false;
}

bool FTPClient::fileExists(String filename) {
    Serial.printf("Checking if file exists: %s\n", filename.c_str());
    
    controlClient.printf("SIZE %s\r\n", filename.c_str());
    String response = readResponse();
    
    return response.startsWith("213");
}

bool FTPClient::createFile(String filename, String content) {
    Serial.printf("Creating new file: %s\n", filename.c_str());
    
    // Set binary mode
    controlClient.printf("TYPE I\r\n");
    readResponse();
    
    // Enter passive mode
    controlClient.printf("PASV\r\n");
    String response = readResponse();
    
    if (!response.startsWith("227")) {
        Serial.println("Failed to enter passive mode");
        return false;
    }
    
    int dataPort;
    if (!parsePassiveMode(response, dataPort)) {
        return false;
    }
    
    // Connect to data port
    if (dataClient.connect(server.c_str(), dataPort)) {
        Serial.printf("Data connection established on port %d\n", dataPort);
        
        // Send STOR command
        controlClient.printf("STOR %s\r\n", filename.c_str());
        readResponse();
        
        // Send data
        dataClient.print(content);
        dataClient.stop();
        
        // Read final response
        readResponse();
        Serial.println("File created successfully");
        return true;
    }
    
    Serial.println("Failed to create file - data connection failed");
    return false;
}

bool FTPClient::appendToFile(String filename, String content) {
    Serial.printf("Appending to file: %s\n", filename.c_str());
    
    // Set binary mode
    controlClient.printf("TYPE I\r\n");
    readResponse();
    
    // Enter passive mode
    controlClient.printf("PASV\r\n");
    String response = readResponse();
    
    if (!response.startsWith("227")) {
        Serial.println("Failed to enter passive mode");
        return false;
    }
    
    int dataPort;
    if (!parsePassiveMode(response, dataPort)) {
        return false;
    }
    
    // Connect to data port
    if (dataClient.connect(server.c_str(), dataPort)) {
        Serial.printf("Data connection established on port %d\n", dataPort);
        
        // Send APPE command
        controlClient.printf("APPE %s\r\n", filename.c_str());
        readResponse();
        
        // Send data
        dataClient.print(content);
        dataClient.stop();
        
        // Read final response
        readResponse();
        Serial.println("Data appended successfully");
        return true;
    }
    
    Serial.println("Failed to append to file - data connection failed");
    return false;
}

bool FTPClient::uploadData(String basePath, String filename, String csvData, bool createHeader) {
    Serial.println("Starting FTP upload process...");
    
    // Connect to FTP server
    if (!connect()) {
        return false;
    }
    
    // Login to FTP server
    if (!login()) {
        disconnect();
        return false;
    }
    
    // Change to target directory
    if (!changeDirectory(basePath)) {
        disconnect();
        return false;
    }
    
    Serial.printf("Filename: %s\n", filename.c_str());
    Serial.printf("CSV data: %s", csvData.c_str());
    
    bool success = false;
    
    // Check if file exists
    if (fileExists(filename)) {
        // File exists, append data
        success = appendToFile(filename, csvData);
    } else {
        // File doesn't exist, create with header if requested
        String fullContent = csvData;
        if (createHeader) {
            String header = "Date,Sample Size,Temp (°C),Pressure (hPa),Humidity (RH%)\r\n";
            fullContent = header + csvData;
        }
        success = createFile(filename, fullContent);
    }
    
    disconnect();
    
    if (success) {
        Serial.println("FTP upload completed successfully!");
    } else {
        Serial.println("FTP upload failed!");
    }
    
    return success;
}
