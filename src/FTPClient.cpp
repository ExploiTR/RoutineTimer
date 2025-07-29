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
            String chunk = controlClient.readString();
            response += chunk;
            
            // Continue reading if there might be more data
            if (chunk.length() > 0) {
                timeout = millis() + 1000; // Extend timeout when data is received
            }
            
            // Check if we have a complete response (ends with \r\n)
            if (response.endsWith("\r\n") || response.endsWith("\n")) {
                break;
            }
        }
        delay(10);
    }
    
    if (response.length() > 0) {
        Serial.print("FTP Response: " + response);
        // Remove any trailing whitespace for cleaner output
        response.trim();
    } else {
        Serial.println("FTP Response: (no response received)");
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
        Serial.printf("Sending %d bytes of data...\n", content.length());
        dataClient.print(content);
        dataClient.flush(); // Ensure all data is sent
        dataClient.stop();
        
        Serial.println("Data connection closed, waiting for final response...");
        
        // Give server time to process the file and send final response
        delay(500);
        
        // Read final response with timeout
        String finalResponse = "";
        unsigned long responseTimeout = millis() + 10000; // 10 second timeout for final response
        
        while (millis() < responseTimeout && finalResponse.length() == 0) {
            finalResponse = readResponse();
            if (finalResponse.length() == 0) {
                delay(100); // Small delay before retrying
            }
        }
        
        Serial.printf("Final response received: '%s'\n", finalResponse.c_str());
        
        if (finalResponse.startsWith("226") || finalResponse.startsWith("250")) {
            Serial.println("File created successfully");
            return true;
        } else if (finalResponse.length() == 0) {
            Serial.println("No final response received - assuming success");
            // Some servers don't send final response - check if file exists
            delay(1000); // Give server time to finalize
            if (fileExists(filename)) {
                Serial.println("File verification successful - creation confirmed");
                return true;
            } else {
                Serial.println("File verification failed - creation likely failed");
                return false;
            }
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
    String typeResponse = readResponse();
    Serial.printf("TYPE I response: %s\n", typeResponse.c_str());
    
    // Enter passive mode
    controlClient.printf("PASV\r\n");
    String response = readResponse();
    
    if (!response.startsWith("227")) {
        Serial.printf("Failed to enter passive mode for download: %s\n", response.c_str());
        return "";
    }
    
    int dataPort;
    if (!parsePassiveMode(response, dataPort)) {
        Serial.println("Failed to parse passive mode response for download");
        return "";
    }
    
    // Connect to data port
    Serial.printf("Attempting data connection to port %d for download\n", dataPort);
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
        
        Serial.println("RETR command accepted, starting file download...");
        
        // Read file content
        String fileContent = "";
        unsigned long timeout = millis() + 30000; // 30 second timeout for download
        int bytesRead = 0;
        bool dataReceived = false;
        
        while (millis() < timeout) {
            if (dataClient.available()) {
                char c = dataClient.read();
                fileContent += c;
                bytesRead++;
                dataReceived = true;
                timeout = millis() + 2000; // Reset timeout when data is received
                
                if (bytesRead % 50 == 0) { // Log every 50 bytes
                    Serial.printf("Downloaded %d bytes so far...\n", bytesRead);
                }
            } else if (!dataClient.connected()) {
                // Only break if connection is closed AND no more data is available
                Serial.println("Data connection closed by server");
                break;
            }
            
            delay(1);
        }
        
        // Make sure we read any remaining buffered data
        while (dataClient.available()) {
            char c = dataClient.read();
            fileContent += c;
            bytesRead++;
        }
        
        Serial.printf("Download completed, total bytes read: %d\n", bytesRead);
        dataClient.stop();
        
        // Read final response
        String finalResponse = readResponse();
        Serial.printf("Final download response: %s\n", finalResponse.c_str());
        
        if (finalResponse.startsWith("226") || finalResponse.startsWith("250")) {
            Serial.printf("File downloaded successfully (%d bytes)\n", fileContent.length());
            Serial.printf("First 100 chars of content: %s\n", fileContent.substring(0, 100).c_str());
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
    
    // Try the upload process up to 2 times (initial attempt + 1 retry)
    for (int attempt = 1; attempt <= 2; attempt++) {
        Serial.printf("Upload attempt %d of 2...\n", attempt);
        
        // Connect to FTP server
        if (!connect()) {
            Serial.printf("Connection failed on attempt %d\n", attempt);
            if (attempt < 2) {
                Serial.println("Retrying in 2 seconds...");
                delay(2000);
                continue;
            }
            return false;
        }
        
        // Login to FTP server
        if (!login()) {
            Serial.printf("Login failed on attempt %d\n", attempt);
            disconnect();
            if (attempt < 2) {
                Serial.println("Retrying in 2 seconds...");
                delay(2000);
                continue;
            }
            return false;
        }
        
        // Change to target directory
        if (!changeDirectory(basePath)) {
            Serial.printf("Directory change failed on attempt %d\n", attempt);
            disconnect();
            if (attempt < 2) {
                Serial.println("Retrying in 2 seconds...");
                delay(2000);
                continue;
            }
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
                Serial.println("Attempting to delete old file...");
                if (deleteFile(filename)) {
                    Serial.println("Old file deleted successfully, creating updated file...");
                    // Create new file with combined content
                    success = createFile(filename, fullContent);
                    if (success) {
                        Serial.println("File updated successfully with appended data");
                    } else {
                        Serial.printf("Failed to create updated file on attempt %d\n", attempt);
                    }
                } else {
                    Serial.printf("Failed to delete old file on attempt %d, cannot update\n", attempt);
                    success = false;
                }
            } else {
                Serial.printf("Failed to download existing file content on attempt %d\n", attempt);
                success = false;
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
            if (!success) {
                Serial.printf("Failed to create new file on attempt %d\n", attempt);
            }
        }
        
        disconnect();
        
        if (success) {
            Serial.printf("FTP upload completed successfully on attempt %d!\n", attempt);
            return true;
        } else {
            Serial.printf("FTP upload failed on attempt %d\n", attempt);
            if (attempt < 2) {
                Serial.println("Will retry entire upload process in 3 seconds...");
                delay(3000);
            }
        }
    }
    
    Serial.println("FTP upload failed after 2 attempts - giving up");
    return false;
}
