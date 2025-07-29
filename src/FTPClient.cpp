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
    
    // Method 1: Try MLST (Machine Listing) command - RFC 3659
    controlClient.printf("MLST %s\r\n", filename.c_str());
    String response = readResponse();
    
    if (response.startsWith("250")) {
        Serial.printf("File exists (found via MLST): %s\n", filename.c_str());
        return true;
    }
    
    // Method 2: Try SIZE command
    Serial.printf("MLST failed, trying SIZE command for: %s\n", filename.c_str());
    controlClient.printf("SIZE %s\r\n", filename.c_str());
    response = readResponse();
    
    if (response.startsWith("213")) {
        Serial.printf("File exists (found via SIZE): %s\n", filename.c_str());
        return true;
    }
    
    // Method 3: If SIZE is not supported, try MDTM (Modification Time)
    if (response.startsWith("550") && response.indexOf("not allowed") != -1) {
        Serial.printf("SIZE not supported, trying MDTM command for: %s\n", filename.c_str());
        controlClient.printf("MDTM %s\r\n", filename.c_str());
        response = readResponse();
        
        if (response.startsWith("213")) {
            Serial.printf("File exists (found via MDTM): %s\n", filename.c_str());
            return true;
        }
    }
    
    Serial.printf("File does not exist: %s\n", filename.c_str());
    return false;
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
        String storResponse = readResponse();
        
        // Check if STOR command was accepted
        if (!storResponse.startsWith("150") && !storResponse.startsWith("125")) {
            Serial.printf("STOR command failed: %s\n", storResponse.c_str());
            dataClient.stop();
            return false;
        }
        
        // Send data
        dataClient.print(content);
        dataClient.stop();
        
        // Read final response
        String finalResponse = readResponse();
        if (finalResponse.startsWith("226") || finalResponse.startsWith("250")) {
            Serial.println("File created successfully");
            return true;
        } else {
            Serial.printf("File creation failed: %s\n", finalResponse.c_str());
            return false;
        }
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
        String appeResponse = readResponse();
        
        // Check if APPE command was accepted
        if (!appeResponse.startsWith("150") && !appeResponse.startsWith("125")) {
            Serial.printf("APPE command failed: %s\n", appeResponse.c_str());
            dataClient.stop();
            
            // If append is not supported, inform caller
            if (appeResponse.startsWith("451") || appeResponse.startsWith("550")) {
                Serial.println("Append not supported by server");
            }
            return false;
        }
        
        // Send data
        dataClient.print(content);
        dataClient.stop();
        
        // Read final response
        String finalResponse = readResponse();
        if (finalResponse.startsWith("226") || finalResponse.startsWith("250")) {
            Serial.println("Data appended successfully");
            return true;
        } else {
            Serial.printf("Append operation failed: %s\n", finalResponse.c_str());
            return false;
        }
    }
    
    Serial.println("Failed to append to file - data connection failed");
    return false;
}

String FTPClient::downloadFile(String filename) {
    Serial.printf("Downloading file: %s\n", filename.c_str());
    
    // Set binary mode
    controlClient.printf("TYPE I\r\n");
    readResponse();
    
    // Enter passive mode
    controlClient.printf("PASV\r\n");
    String response = readResponse();
    
    if (!response.startsWith("227")) {
        Serial.println("Failed to enter passive mode for download");
        return "";
    }
    
    int dataPort;
    if (!parsePassiveMode(response, dataPort)) {
        return "";
    }
    
    // Connect to data port
    if (dataClient.connect(server.c_str(), dataPort)) {
        Serial.printf("Data connection established on port %d for download\n", dataPort);
        
        // Send RETR command
        controlClient.printf("RETR %s\r\n", filename.c_str());
        String retrResponse = readResponse();
        
        // Check if RETR command was accepted
        if (!retrResponse.startsWith("150") && !retrResponse.startsWith("125")) {
            Serial.printf("RETR command failed: %s\n", retrResponse.c_str());
            dataClient.stop();
            return "";
        }
        
        // Read file content
        String fileContent = "";
        unsigned long timeout = millis() + 30000; // 30 second timeout for download
        
        while (dataClient.connected() && millis() < timeout) {
            if (dataClient.available()) {
                fileContent += dataClient.readString();
                timeout = millis() + 5000; // Reset timeout when data is received
            }
            delay(10);
        }
        
        dataClient.stop();
        
        // Read final response
        String finalResponse = readResponse();
        if (finalResponse.startsWith("226") || finalResponse.startsWith("250")) {
            Serial.printf("File downloaded successfully (%d bytes)\n", fileContent.length());
            return fileContent;
        } else {
            Serial.printf("File download failed: %s\n", finalResponse.c_str());
            return "";
        }
    }
    
    Serial.println("Failed to download file - data connection failed");
    return "";
}

bool FTPClient::deleteFile(String filename) {
    Serial.printf("Deleting file: %s\n", filename.c_str());
    
    controlClient.printf("DELE %s\r\n", filename.c_str());
    String response = readResponse();
    
    if (response.startsWith("250")) {
        Serial.println("File deleted successfully");
        return true;
    } else {
        Serial.printf("File deletion failed: %s\n", response.c_str());
        return false;
    }
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
    bool exists = fileExists(filename);
    Serial.printf("File existence check result: %s\n", exists ? "EXISTS" : "NOT FOUND");
    
    if (exists) {
        // File exists - download, append data, delete old, create new
        Serial.println("File exists - downloading existing content to append new data");
        
        String existingContent = downloadFile(filename);
        if (existingContent.length() > 0) {
            Serial.printf("Downloaded %d bytes of existing data\n", existingContent.length());
            
            // Append new data to existing content
            String fullContent = existingContent + csvData;
            
            Serial.printf("Total content size after append: %d bytes\n", fullContent.length());
            
            // Delete the old file
            if (deleteFile(filename)) {
                // Create new file with combined content
                success = createFile(filename, fullContent);
                if (success) {
                    Serial.println("File updated successfully with appended data");
                }
            } else {
                Serial.println("Failed to delete old file, cannot update");
                success = false;
            }
        } else {
            Serial.println("Failed to download existing file content");
            // Fallback: create timestamped file
            time_t now = time(nullptr);
            struct tm* timeinfo = localtime(&now);
            char timestamp[20];
            strftime(timestamp, sizeof(timestamp), "%H%M%S", timeinfo);
            
            int dotIndex = filename.lastIndexOf('.');
            String baseName = filename.substring(0, dotIndex);
            String extension = filename.substring(dotIndex);
            String uniqueFilename = baseName + "_" + String(timestamp) + extension;
            
            Serial.printf("Creating fallback timestamped file: %s\n", uniqueFilename.c_str());
            
            String fullContent = csvData;
            if (createHeader) {
                String header = "Date,Sample Size,Temp (°C),Pressure (hPa),Humidity (RH%)\r\n";
                fullContent = header + csvData;
                Serial.println("Header added to fallback file");
            }
            success = createFile(uniqueFilename, fullContent);
        }
    } else {
        // File doesn't exist, create with header if requested
        Serial.println("File does not exist - creating new file");
        String fullContent = csvData;
        if (createHeader) {
            String header = "Date,Sample Size,Temp (°C),Pressure (hPa),Humidity (RH%)\r\n";
            fullContent = header + csvData;
            Serial.println("Header added to new file");
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
