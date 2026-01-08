#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include <winsock2.h>
#include <string>
#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <vector>
#pragma comment(lib, "ws2_32.lib")

using namespace std;

bool receiveAll(SOCKET socket, char* buffer, int length);
bool sendAll(SOCKET socket, char* buffer, int length);
bool validChars(string line);
void logAndOptionallyClose(ofstream& logFile, string message, bool close, SOCKET socket);
void log(ofstream& logFile, string logMessage);

int main(int argc, char* argv[]) {
    WSADATA wsa;
    SOCKET clientSocket = INVALID_SOCKET;
    struct sockaddr_in serverAddress;

    string ipAddress;
    int port = -1;
    string fileName;

    //Obtain and store arguments
    for (int i = 1; i < argc - 1; ++i)
    {
        if (strcmp(argv[i], "-a") == 0 && i + 1 < argc)
            ipAddress = argv[++i];
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            port = atoi(argv[++i]);
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc)
            fileName = argv[++i];
        else
        {
            cout << "Usage: -a <IP> -p <Port> -f <File.txt>" << endl;
            return 1;
        }
    }

    //Make sure arguments are valid 
    if (ipAddress.empty())
    {
        cout << "Error: Empty IP address" << endl;
        return 1;
    }
    if (port <= 1024)
    {
        cout << "Error: Port number must be greater than 1024" << endl;
        return 1;
    }
    if (fileName.empty())
    {
        cout << "Error: Empty fileName" << endl;
        return 1;
    }


    //Open log file
    ofstream logFile("clientLog.txt", ios::trunc);
    if (!logFile)
    {
        cout << "Error: Log file not opened" << endl;
        return 1;
    }
    log(logFile, "Log file opened\nProgram started\n");

    //Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        log(logFile, "Error: Failed to start Winsock\n");
        return 1;
    }
    log(logFile, "Winsock Initialized\n");

    //Open input file
    ifstream file;
    file.open(fileName);
    if (!file.is_open())
    {
        logAndOptionallyClose(logFile, "Error: Input file not opened\n", false, clientSocket);
        WSACleanup();
        return 1;
    }
    logAndOptionallyClose(logFile, "Input file opened successfully\n\n", false, clientSocket);


    //Loop for input lines in file
    string line;
    while (getline(file, line))
    {
        if (line.empty()) //Skip line if empty
        {
            logAndOptionallyClose(logFile, "Error: Line was empty, moving on to next line\n\n", false, clientSocket);
            continue;
        }

        //Skip line if it contains invalid characters (Not within hex 20 and 7E)
        if (!validChars(line))
        {
            logAndOptionallyClose(logFile, "Error: Line contains ASCII character(s) outside the printable bounds, moving on to next line\n\n", false, clientSocket);
            continue;
        }

        //Create client's socket
        clientSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (clientSocket == INVALID_SOCKET)
        {
            logAndOptionallyClose(logFile, "Error: Socket creation failed, moving on to next line\n\n", false, clientSocket);
            continue;
        }
        log(logFile, "Socket creation successful\n");

        //Set client's address details
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_addr.s_addr = inet_addr(ipAddress.c_str());
        serverAddress.sin_port = htons(port);

        //Connect client socket to server's address/port
        if (connect(clientSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR)
        {
            logAndOptionallyClose(logFile, "Error: Could not connect to server, moving on to next line\n\n", true, clientSocket);
            continue;
        }
        logAndOptionallyClose(logFile, "Connected to server\n", false, clientSocket);


        //Get value of N (number of hash requests)
        int n = (line.length() + 15) / 16;

        char initBuffer[6] = { 0 };
        //Network byte order for type and n
        uint16_t type = htons(1);
        uint32_t N = htonl(n);
        //Insert type with value 1 (2 bytes) and num of hash requests N (4 bytes) into initialization buffer
        memcpy(initBuffer, &type, 2);
        memcpy(initBuffer + 2, &N, 4);

        //Send initialization message to server
        if (!sendAll(clientSocket, initBuffer, sizeof(initBuffer)))
        {
            logAndOptionallyClose(logFile, "Error: Could not send initialization message to server, moving on to next line\n\n", true, clientSocket);
            continue;
        }
        logAndOptionallyClose(logFile, "Sent initialization message to server\n", false, clientSocket);


        //Receive Acknowledgement
        char ackBuffer[6];
        if (!receiveAll(clientSocket, ackBuffer, sizeof(ackBuffer)))
        {
            logAndOptionallyClose(logFile, "Error: Did not receive full acknowledgement or recv() failed, moving on to next line\n\n", true, clientSocket);
            continue;
        }

        uint16_t ackType;
        uint32_t expectedResponseLength;
        //Extract info from ackBuffer and store in ackType and expectedResponseLength
        memcpy(&ackType, ackBuffer, 2);
        memcpy(&expectedResponseLength, ackBuffer + 2, 4);
        //Convert to host byte order
        ackType = ntohs(ackType);
        expectedResponseLength = ntohl(expectedResponseLength);

        if (ackType != 2) //Wrong message type
        {
            logAndOptionallyClose(logFile, "Error: Expected Acknowledgement message (Type 2), received Type " + to_string(ackType) + " instead, moving on to next line\n\n", true, clientSocket);
            continue;
        }
        if (expectedResponseLength != n * 38) //Wrong total length
        {
            logAndOptionallyClose(logFile, "Error: " + to_string(n * 38) + " bytes is expected in response to " + to_string(n) + " requests. Server wants to send " + to_string(expectedResponseLength) + " instead, moving on to next line\n\n", true, clientSocket);
            continue;
        }
        logAndOptionallyClose(logFile, "Received acknowledgement message from server. Expecting " + to_string(expectedResponseLength) + " total bytes in response\n", false, clientSocket);

        //Separate into 16 byte segments
        vector<string> segments;
        for (int i = 0; i < n; i++)
            segments.push_back(line.substr(i * 16, 16));

        bool continueLoop = false;
        //Send hash requests
        for (int i = 0; i < segments.size(); i++)
        {
            const string& segment = segments.at(i);
            int length = segment.length();

            //Network byte order for type and length
            uint16_t type = htons(3);
            uint32_t payloadLength = htonl(length);
            //Create message as char vector
            int messageSize = 2 + 4 + length; //2 for type, 4 for length, length for payload
            vector<char> message(messageSize);
            memcpy(message.data(), &type, 2);
            memcpy(message.data() + 2, &payloadLength, 4);
            memcpy(message.data() + 6, segment.c_str(), length);

            //Send to server
            if (!sendAll(clientSocket, message.data(), message.size()))
            {
                logAndOptionallyClose(logFile, "Error: Could not send hash request to server, moving on to next line\n\n", true, clientSocket);
                continueLoop = true;
                break;
            }
            logAndOptionallyClose(logFile, "Sent hashRequest for segment: '" + segment + "' of length " + to_string(length) + " bytes\n", false, clientSocket);
        }
        if (continueLoop)
            continue;


        //Receive hash responses
        vector<char> recvBuffer(expectedResponseLength);
        if (!receiveAll(clientSocket, recvBuffer.data(), recvBuffer.size()))
        {
            logAndOptionallyClose(logFile, "Error: Error receiving hash responses, moving on to next line\n\n", true, clientSocket);
            continue;
        }

        //Process HashReponses from server
        vector<string> responses(n);
        for (int i = 0; i < n; i++)
        {
            //Points to start of ith (38 byte) hash response 
            char* start = &recvBuffer[i * 38];
            uint16_t type;
            uint32_t index;
            //Extract type and index from recvBuffer beginning at start and store
            memcpy(&type, start, 2);
            memcpy(&index, start + 2, 4);
            //Convert to host byte order
            type = ntohs(type);
            index = ntohl(index);

            if (type != 4) //Wrong message type
            {
                logAndOptionallyClose(logFile, "Error: Expected HashResponse (Type 4), received type " + to_string(type) + " instead, moving on to next line\n\n", true, clientSocket);
                continueLoop = true;
                break;
            }
            log(logFile, "Received hash response #" + to_string(i) + " from server\n");

            //Extract the 32 hex characters representing the hashed payload
            char hexBuffer[33];
            memcpy(hexBuffer, start + 6, 32);
            hexBuffer[32] = '\0';

            //Store hex string in responses vector at proper index to keep proper order
            if (index < n)
                responses.at(index) = hexBuffer;
        }
        if (continueLoop)
            continue;

        //Display responses
        cout << "Hash Responses:" << endl;
        for (int i = 0; i < n; i++)
            logAndOptionallyClose(logFile, to_string(i) + ": " + (responses.at(i).empty() ? "Invalid Response" : ("0x" + responses.at(i))) + "\n", false, clientSocket);

        //Close socket and move on to next line in file
        logAndOptionallyClose(logFile, "Closing connection and moving to next line\n\n", true, clientSocket);
    }
    file.close();
    logAndOptionallyClose(logFile, "End of input file reached. Closing input file\n", false, clientSocket);
    log(logFile, "Closing log file\n");
    logFile.close();
    cout << "Log file closed\n";

    WSACleanup();
    return 0;
}

//Ensures all bytes have been received
bool receiveAll(SOCKET socket, char* buffer, int length)
{
    for (int totalReceived = 0; totalReceived < length;)
    {
        int received = recv(socket, buffer + totalReceived, length - totalReceived, 0);
        if (received <= 0)
            return false; //Failure
        totalReceived += received;
    }
    return true; //Success
}

//Ensures all bytes have been sent
bool sendAll(SOCKET socket, char* buffer, int length)
{
    for (int totalSent = 0; totalSent < length;)
    {
        int sent = send(socket, buffer + totalSent, length - totalSent, 0);
        if (sent <= 0)
            return false; //Failure
        totalSent += sent;
    }
    return true; //Success
}

//Make sure all characters in the line are ASCII characters hex 20 - 7E
bool validChars(string line)
{
    for (char c : line)
    {
        if (c < 0x20 || c > 0x7E)
            return false;
    }
    return true;
}

//Print message to console, log message to file, and close socket if requested
void logAndOptionallyClose(ofstream& logFile, string message, bool close, SOCKET socket)
{
    cout << message;
    log(logFile, message);
    if (close)
        closesocket(socket);
}

//Print log message to file and flush to ensure instant log
void log(ofstream& logFile, string logMessage)
{
    logFile << logMessage;
    logFile.flush();
}