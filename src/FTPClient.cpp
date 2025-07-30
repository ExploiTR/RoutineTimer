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
        
        // Send data in chunks to ensure reliability for large files
        size_t totalSent = 0;
        size_t chunkSize = 512; // Send in 512 byte chunks
        
        while (totalSent < content.length()) {
            size_t remaining = content.length() - totalSent;
            size_t toSend = (remaining < chunkSize) ? remaining : chunkSize;
            
            String chunk = content.substring(totalSent, totalSent + toSend);
            size_t sent = dataClient.print(chunk);
            
            if (sent != chunk.length()) {
                Serial.printf("ERROR: Data send incomplete - tried %d, sent %d\n", chunk.length(), sent);
                dataClient.stop();
                return false;
            }
            
            totalSent += sent;
            
            // Progress logging for large files
            if (totalSent % 1024 == 0 || totalSent == content.length()) {
                Serial.printf("Sent %d of %d bytes (%.1f%%)\n", totalSent, content.length(), 
                             (float)totalSent / content.length() * 100);
            }
            
            delay(1); // Small delay to prevent overwhelming the connection
        }
        
        dataClient.flush(); // Ensure all data is sent
        Serial.printf("All %d bytes sent successfully\n", totalSent);
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

bool FTPClient::renameFile(String oldName, String newName) {
    Serial.printf("Renaming file from %s to %s\n", oldName.c_str(), newName.c_str());
    
    // Send RNFR (rename from) command
    controlClient.printf("RNFR %s\r\n", oldName.c_str());
    String response = readResponse();
    
    if (!response.startsWith("350")) {
        Serial.printf("RNFR command failed: %s\n", response.c_str());
        return false;
    }
    
    // Send RNTO (rename to) command
    controlClient.printf("RNTO %s\r\n", newName.c_str());
    response = readResponse();
    
    if (response.startsWith("250")) {
        Serial.println("File renamed successfully");
        return true;
    } else {
        Serial.printf("RNTO command failed: %s\n", response.c_str());
        return false;
    }
}

bool FTPClient::safeDeleteFile(String filename) {
    Serial.printf("Safe delete for file: %s\n", filename.c_str());
    
    // Keep trying to delete until successful or max attempts reached
    int maxAttempts = 5;
    for (int attempt = 1; attempt <= maxAttempts; attempt++) {
        Serial.printf("Delete attempt %d of %d for file: %s\n", attempt, maxAttempts, filename.c_str());
        
        // Check connection before each attempt
        if (!isConnected()) {
            Serial.println("ERROR: Connection lost during delete operation");
            return false;
        }
        
        if (deleteFile(filename)) {
            // Verify the file is actually gone
            delay(500); // Give server time to process
            
            if (!fileExists(filename)) {
                Serial.printf("File %s successfully deleted and verified gone\n", filename.c_str());
                return true;
            } else {
                Serial.printf("File %s reported deleted but still exists, retrying...\n", filename.c_str());
                delay(1000);
                continue;
            }
        } else {
            Serial.printf("Delete attempt %d failed for file: %s\n", attempt, filename.c_str());
            if (attempt < maxAttempts) {
                delay(1000);
            }
        }
    }
    
    // CRITICAL: After 5 failed delete attempts, rename to .bak to get it out of the way
    Serial.printf("CRITICAL: Failed to delete %s after %d attempts - trying backup strategy\n", filename.c_str(), maxAttempts);
    
    // Find a unique backup name (handle multiple .bak files)
    String baseName = filename.substring(0, filename.lastIndexOf('.'));
    String backupName = baseName + ".bak";
    int backupCounter = 1;
    
    // Check if .bak already exists, use .bak1, .bak2, etc.
    while (fileExists(backupName)) {
        backupName = baseName + ".bak" + String(backupCounter);
        backupCounter++;
        Serial.printf("Backup file exists, trying: %s\n", backupName.c_str());
        
        // Safety limit to prevent infinite loop
        if (backupCounter > 10) {
            Serial.println("FATAL: Too many backup files - manual cleanup required");
            return false;
        }
    }
    
    Serial.printf("Attempting to rename %s to %s to clear the way\n", filename.c_str(), backupName.c_str());
    
    // Try to rename to backup - this MUST succeed or we're stuck
    for (int renameAttempt = 1; renameAttempt <= 3; renameAttempt++) {
        Serial.printf("Backup rename attempt %d of 3\n", renameAttempt);
        
        // Check connection before rename attempt
        if (!isConnected()) {
            Serial.println("ERROR: Connection lost during backup rename");
            return false;
        }
        
        if (renameFile(filename, backupName)) {
            Serial.printf("SUCCESS: Renamed %s to %s - original path is now clear\n", filename.c_str(), backupName.c_str());
            Serial.println("WARNING: Old data preserved in .bak file - manual cleanup needed later");
            return true; // We cleared the path, mission accomplished
        } else {
            Serial.printf("Backup rename attempt %d failed\n", renameAttempt);
            if (renameAttempt < 3) {
                delay(2000); // Longer delay for rename attempts
            }
        }
    }
    
    Serial.printf("FATAL: Could not delete OR rename %s - upload cannot proceed safely\n", filename.c_str());
    Serial.println("FATAL: Manual intervention required - file is stuck on server");
    return false;
}

bool FTPClient::isConnected() {
    return controlClient.connected();
}

bool FTPClient::uploadData(String basePath, String filename, String csvData, bool createHeader) {
    Serial.println("Starting SAFE FTP upload process...");
    Serial.printf("Target file: %s\n", filename.c_str());
    Serial.printf("New data to add: %s", csvData.c_str());
    
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
        
        bool success = false;
        String tempFilename = filename.substring(0, filename.lastIndexOf('.')) + "_new.csv";
        
        // SAFE FLOW: Check if original file exists
        bool originalExists = fileExists(filename);
        Serial.printf("Original file existence: %s\n", originalExists ? "EXISTS" : "NOT FOUND");
        
        String fullContent = "";
        
        if (originalExists) {
            // STEP 1: Download existing file content
            Serial.println("STEP 1: Downloading existing file content...");
            
            // Check available memory before download
            #ifdef ESP32
            size_t freeHeap = ESP.getFreeHeap();
            Serial.printf("Free heap before download: %d bytes\n", freeHeap);
            if (freeHeap < 10240) { // Less than 10KB free
                Serial.println("ERROR: Insufficient memory for safe operation");
                disconnect();
                if (attempt < 2) {
                    Serial.println("Retrying in 3 seconds...");
                    delay(3000);
                    continue;
                }
                return false;
            }
            #endif
            
            String existingContent = downloadFile(filename);
            
            if (existingContent.length() == 0) {
                Serial.printf("ERROR: Failed to download existing file content on attempt %d\n", attempt);
                disconnect();
                if (attempt < 2) {
                    Serial.println("Retrying in 3 seconds...");
                    delay(3000);
                    continue;
                }
                return false;
            }
            
            Serial.printf("Successfully downloaded %d bytes of existing data\n", existingContent.length());
            
            // STEP 2: Combine existing content with new data
            Serial.println("STEP 2: Combining existing content with new data...");
            
            // Check memory before combining
            #ifdef ESP32
            freeHeap = ESP.getFreeHeap();
            Serial.printf("Free heap before combine: %d bytes\n", freeHeap);
            size_t estimatedSize = existingContent.length() + csvData.length() + 1000; // +1000 buffer
            if (freeHeap < estimatedSize * 2) { // Need 2x for string operations
                Serial.printf("ERROR: Insufficient memory for combine operation (need ~%d, have %d)\n", 
                             estimatedSize * 2, freeHeap);
                disconnect();
                if (attempt < 2) {
                    Serial.println("Retrying in 3 seconds...");
                    delay(3000);
                    continue;
                }
                return false;
            }
            #endif
            
            fullContent = existingContent + csvData;
            Serial.printf("Total content size: %d bytes\n", fullContent.length());
            
        } else {
            // File doesn't exist - create new content with header if requested
            Serial.println("Original file doesn't exist - creating new content");
            fullContent = csvData;
            if (createHeader) {
                String header = "Date,Sample Size,Temp (°C),Pressure (hPa),Humidity (RH%)\r\n";
                fullContent = header + csvData;
                Serial.println("Added header to new file content");
            }
        }
        
        // STEP 3: Create temporary file with combined content
        Serial.printf("STEP 3: Creating temporary file: %s\n", tempFilename.c_str());
        if (!createFile(tempFilename, fullContent)) {
            Serial.printf("ERROR: Failed to create temporary file on attempt %d\n", attempt);
            disconnect();
            if (attempt < 2) {
                Serial.println("Retrying in 3 seconds...");
                delay(3000);
                continue;
            }
            return false;
        }
        
        Serial.println("Temporary file created successfully");
        
        // STEP 3.5: VERIFY temp file content by downloading it back
        Serial.println("STEP 3.5: Verifying temporary file content...");
        
        // Give server extra time to flush file to disk
        delay(2000);
        
        // Check connection before verification
        if (!isConnected()) {
            Serial.println("ERROR: Connection lost before temp file verification");
            disconnect();
            if (attempt < 2) {
                Serial.println("Retrying in 3 seconds...");
                delay(3000);
                continue;
            }
            return false;
        }
        
        String verifyContent = downloadFile(tempFilename);
        
        if (verifyContent.length() != fullContent.length()) {
            Serial.printf("ERROR: Temp file verification failed - size mismatch (expected %d, got %d)\n", 
                         fullContent.length(), verifyContent.length());
            
            // Clean up bad temp file
            deleteFile(tempFilename);
            disconnect();
            if (attempt < 2) {
                Serial.println("Retrying in 3 seconds...");
                delay(3000);
                continue;
            }
            return false;
        }
        
        Serial.printf("Temp file verified successfully (%d bytes)\n", verifyContent.length());
        
        // STEP 4: If original file exists, safely delete it
        if (originalExists) {
            Serial.printf("STEP 4: Safely deleting original file: %s\n", filename.c_str());
            
            // Check connection before critical delete operation
            if (!isConnected()) {
                Serial.println("ERROR: Connection lost before delete operation");
                disconnect();
                if (attempt < 2) {
                    Serial.println("Retrying in 3 seconds...");
                    delay(3000);
                    continue;
                }
                return false;
            }
            
            if (!safeDeleteFile(filename)) {
                Serial.printf("ERROR: Failed to safely delete original file on attempt %d\n", attempt);
                
                // Clean up: try to delete the temporary file
                Serial.println("Cleaning up: deleting temporary file...");
                if (!deleteFile(tempFilename)) {
                    Serial.printf("WARNING: Failed to clean up temp file: %s\n", tempFilename.c_str());
                    Serial.println("WARNING: Manual cleanup of temp file may be needed");
                }
                
                disconnect();
                if (attempt < 2) {
                    Serial.println("Retrying in 3 seconds...");
                    delay(3000);
                    continue;
                }
                return false;
            }
            
            Serial.println("Original file safely deleted and verified gone");
        }
        
        // STEP 5: Rename temporary file to original filename
        Serial.printf("STEP 5: Renaming %s to %s\n", tempFilename.c_str(), filename.c_str());
        
        // Check connection before critical rename operation
        if (!isConnected()) {
            Serial.println("ERROR: Connection lost before rename operation");
            disconnect();
            if (attempt < 2) {
                Serial.println("Retrying in 3 seconds...");
                delay(3000);
                continue;
            }
            return false;
        }
        
        // Double-check that original path is clear before rename
        if (fileExists(filename)) {
            Serial.printf("ERROR: Original file reappeared before rename: %s\n", filename.c_str());
            Serial.println("CRITICAL: Race condition detected - aborting this attempt");
            disconnect();
            if (attempt < 2) {
                Serial.println("Retrying in 3 seconds...");
                delay(3000);
                continue;
            }
            return false;
        }
        
        if (!renameFile(tempFilename, filename)) {
            Serial.printf("ERROR: Failed to rename temporary file on attempt %d\n", attempt);
            
            // Critical error - we have the temp file but can't rename it
            // Try to clean up, but this is a serious problem
            Serial.println("CRITICAL: Temporary file exists but couldn't be renamed!");
            Serial.printf("Manual intervention may be needed - temp file: %s\n", tempFilename.c_str());
            
            disconnect();
            if (attempt < 2) {
                Serial.println("Retrying in 3 seconds...");
                delay(3000);
                continue;
            }
            return false;
        }
        
        Serial.println("File successfully renamed to final filename");
        
        // STEP 6: Verify final file exists and has correct content
        Serial.println("STEP 6: Verifying final file...");
        delay(1000); // Give server time to finalize
        
        if (fileExists(filename)) {
            Serial.println("Final file exists - performing content verification...");
            
            // Verify the final file has the correct content
            String finalContent = downloadFile(filename);
            if (finalContent.length() == fullContent.length()) {
                Serial.printf("SUCCESS: Final file verified with correct size (%d bytes)\n", finalContent.length());
                Serial.println("SUCCESS: Final file exists and upload completed safely!");
                success = true;
            } else {
                Serial.printf("ERROR: Final file size mismatch (expected %d, got %d)\n", 
                             fullContent.length(), finalContent.length());
                Serial.println("ERROR: Final file verification failed - content corrupted");
                success = false;
            }
        } else {
            Serial.printf("ERROR: Final file verification failed on attempt %d\n", attempt);
            success = false;
        }
        
        disconnect();
        
        if (success) {
            Serial.printf("SAFE FTP upload completed successfully on attempt %d!\n", attempt);
            Serial.println("Data safety guaranteed - no risk of data loss!");
            return true;
        } else {
            Serial.printf("SAFE FTP upload failed on attempt %d\n", attempt);
            if (attempt < 2) {
                Serial.println("Will retry entire safe upload process in 3 seconds...");
                delay(3000);
            }
        }
    }
    
    Serial.println("SAFE FTP upload failed after 2 attempts - giving up");
    return false;
}
