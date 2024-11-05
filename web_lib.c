#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <sys/time.h>
#include <net/if.h>
#include <sys/stat.h>
#include "web_lib.h"  

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
<-----------------------------WEB FUNCTIONS-------------------------------------> 
*/

char* get_status_message_from_code(int status_code); // return appropriate HTTP status message

char* get_ctype_from_file(char* filename); // returns content-type header from requested file

int bind_server_setup(int port, in_addr_t address, struct sockaddr_in* myaddr); // binds server process to port and starts

HttpRequest* parse_http_request(char* message); // handles parsing of HTTP request message

char* build_http_response_header(size_t file_content_length, char* filename, int status_code, char* http_prtcl, int keep_alive, char hostname[256]); // constructs HTTP response

int leave_conn_open(int connfd, HttpRequest* request); // checks if connection should stay open (if HTTP/1.1 and keep-alive)

char* find_filepath(char* filepath); // modifies filepath directory to look within working directory (check header file) and checks if file exists, if not returns appropiate html file

size_t get_file_size(char* filepath); // returns file size

void* send_file(void* args); // sends file in increments of FILE_BUF_SIZE


/*
<----------------------------------CODE----------------------------------------->
*/

char* get_status_message_from_code(int status_code){

    char* result = malloc(64);
    char* message;
    switch (status_code){
        case 200:
            message = "OK";
            break;
        case 400:
            message = "Bad Request";
            break;
        case 401:
            message = "Unauthorized";
            break;
        case 403:
            message = "Forbidden";
            break;
        case 404:
            message = "Not Found";
            break;
        case 500:
            message = "Internal Server Error";
            break;
        case 505:
            message = "HTTP Version Not Supported";
            break;
        default:
            perror("Unsupported Response Code");
            message = "Unsupported Response Code";
            break;
    }
    strcpy(result, message);
    return result;
}

char* get_ctype_from_file(char* filename) {
    char* extension = strrchr(filename, '.');
    if (extension == NULL) {
        return "application/octet-stream"; // No extension found, return binary data
    }

    if (strcmp(extension, ".html") == 0) {
        return "text/html; charset=UTF-8";
    } else if (strcmp(extension, ".jpg") == 0 || strcmp(extension, ".jpeg") == 0) {
        return "image/jpeg";
    } else if (strcmp(extension, ".png") == 0) {
        return "image/png";
    } else if (strcmp(extension, ".gif") == 0) {
        return "image/gif";
    } else if (strcmp(extension, ".ico") == 0) { // supports favicons
        return "image/x-icon";
    } else {
        return "application/octet-stream"; // For unknown extensions
    }
}

// Reads file within current directory by filename and returns file contents as string (and computes the file size as # of chars/file size and saves to pointer)
// This frees filepath (argument) and returns a completely new string
char* find_filepath(char* filepath) {
    
    if (filepath == NULL) {
        filepath = "error.html";
    }

    // client tried to access directory above root (or working directory)

    // modify to our working directory
    char* appended_filepath = malloc(strlen(filepath) + strlen(WORKING_DIRECTORY) + 1); // allocate enough memory to prepend our working directory
    strcpy(appended_filepath, WORKING_DIRECTORY);
    strcat(appended_filepath, filepath);
    
    // last check to make sure file exists in working dir
    int fd = open(appended_filepath, O_RDONLY);
    if (fd == -1) {
        if (errno == 13) {
            filepath = "forbidden.html";
            memset(appended_filepath, 0, sizeof(appended_filepath));
            strcpy(appended_filepath, WORKING_DIRECTORY);
            strcat(appended_filepath, filepath);
        }
        else {
            perror("Error opening file: File not found");
            return NULL; 
        }
    }
    close(fd);

    return appended_filepath;
}

char* get_http_date() {
    time_t curr = time(NULL);
    struct tm* time = gmtime(&curr);
    char* result = malloc(64);
    strftime(result, 64, "%a, %d %b %Y %H:%M:%S GMT", time);
    return result;
}

size_t get_file_size(char* filepath) {
    // Open the file
    int file_fd = open(filepath, O_RDONLY);
    if (file_fd < 0) {
        perror("Failed to open file");
        return -1;
    }

    struct stat file_stat;
    if (fstat(file_fd, &file_stat) < 0) {
        perror("Failed to get file size");
        close(file_fd);
        return -1;
    }

    return (size_t)file_stat.st_size;
}

void* send_file(void* args) {
    // convert args
    SendFileArgs* new_args = (SendFileArgs*)args;
    char* filepath = new_args->filepath;
    int connfd = new_args->socket;
    atomic_int* tracker = new_args->tracker;
    atomic_fetch_add(tracker, 1);
    // Open the file
    int file_fd = open(filepath, O_RDONLY);
    if (file_fd < 0) {
        perror("Failed to open file");
    }

    char buffer[FILE_BUF_SIZE]; // Buffer for file reading
    ssize_t bytes_read;

    // Send file content in chunks
    while ((bytes_read = read(file_fd, buffer, sizeof(buffer))) > 0) {
        if (bytes_read < 0) {
            perror("Error reading file");
            close(file_fd);
        }

        // TODO fix SEGPIPE thing
        if (send(connfd, buffer, bytes_read, MSG_NOSIGNAL) < 0) {
            perror("Failed to send chunk data");
            close(file_fd);
        }
        memset(buffer, 0, sizeof(buffer)); // Clear the buffer for the next chunk
    }

    close(file_fd);
    atomic_fetch_sub(tracker, 1);
    return 0;
}

// Given a port and IP address, binds to port and returns the socket descriptor or -1 if failed
int bind_server_setup(int port, in_addr_t address, struct sockaddr_in* myaddr) {
    int sock = socket(AF_INET, SOCK_STREAM, 0); 
    if (sock < 0) {
        perror("Failed to create socket");
        return -1;
    }

    // Set socket option to reuse the address
    int optval = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) < 0) {
        perror("setsockopt failed");
        close(sock);
        return -1;
    }

    myaddr->sin_port = htons(port);
    myaddr->sin_family = AF_INET;
    myaddr->sin_addr.s_addr = htonl(address);

    // Bind the socket to the provided IP and port
    if (bind(sock, (struct sockaddr*)myaddr, sizeof(*myaddr)) < 0) {
        perror("Bind failed");
        close(sock);
        return -1;
    }

    return sock; // Return the bound socket file descriptor
}

char* build_http_response_header(size_t file_content_length, char* filename, int status_code, char* http_prtcl, int keep_alive, char hostname[256]){
    char* response;
    int build_result;
    char* status_message = get_status_message_from_code(status_code);
    response = malloc(HTTP_BUF_SIZE);

    if (response == NULL) {
        perror("Failed to allocate memory for response");
        return NULL;
    }
    char* content_type = get_ctype_from_file(filename);
    int cmp_result = strcmp(http_prtcl, "HTTP/1.1");
    char* date = get_http_date();

    if (cmp_result == 0){
        // handle 1.1 header here TODO HOSTNAME
        build_result = sprintf(response,
        "%s %d %s\r\n" 
        "Host: %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Date: %s\r\n"
        "Connection: %s\r\n" // Add Connection header
        "\r\n",
        http_prtcl, status_code, status_message, hostname, content_type, file_content_length, date,
        keep_alive ? "keep-alive" : "close");
    } else {
        // 1.0 Header
        build_result = sprintf(response,
            "%s %d %s\r\n" // Status line
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "\r\n",
            http_prtcl, status_code, status_message, content_type, file_content_length);
    }

    // check for buffer overflow
    if (build_result < 0){
        perror("Error building request (buffer error)");
        return NULL;
    }

    return response;
}

int leave_conn_open(int connfd, HttpRequest* request){
    if ((strcmp(request->http_protocol, "HTTP/1.1") == 0) && (request->keep_alive == 1)){
        return 1;
    }
    return 0;
}

HttpRequest* parse_http_request(char* message) {
    HttpRequest* result = malloc(sizeof(HttpRequest));
    if (result == NULL) {
        perror("Memory allocation failed for HttpRequest");
        return NULL;
    }

    char http_method[64];
    char file_path[512];
    char protocol[64];
    char host[256];
    
    // Attempt to parse the request line
    if (sscanf(message, "%s %s %s", http_method, file_path, protocol) < 0) {
        perror("Error parsing request: insufficient parameters");
        free(result);  // Free the allocated memory
        return NULL;
    }

    char* host_header = strstr(message, "Host:");
    if (host_header != NULL) {
        sscanf(host_header, "Host: %127s", host);
        strcpy(result->hostname, host);
    }

    // Remove leading slash from file_path if present
    if (file_path[0] == '/') {
        memmove(file_path, file_path + 1, strlen(file_path)); // Shift characters
    }

    // Default to "index.html" if no file path provided
    if (strlen(file_path) < 1) {
        strcpy(file_path, "index.html");
    }

    strcpy(result->file_path, file_path);
    strcpy(result->http_method, http_method);
    strcpy(result->http_protocol, protocol);

    // Check for keep-alive in the message
    if (strstr(message, "Connection: keep-alive") != NULL) {
        result->keep_alive = 1;
    } else {
        result->keep_alive = 0;
    }

    return result;
}
