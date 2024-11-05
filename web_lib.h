#ifndef WEB_LIB_H
#define WEB_LIB_H

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

#define FILE_BUF_SIZE 64000
#define HTTP_BUF_SIZE 4096
#define MAX_LEN_FILE_PATH 256
#define WORKING_DIRECTORY "content/" // files

/* STRUCTS */

// Used to keep track of important HttpRequest parameters
typedef struct {
    char http_method[16];
    char file_path[MAX_LEN_FILE_PATH];
    char http_protocol[16];
    int keep_alive;
    char hostname[256];
} HttpRequest;

// Struct to hold the socket and pass the thread/client tracker pointer to each thread
typedef struct {
    int socket;
    atomic_int* thread_tracker;
} ThreadArgs;

typedef struct {
    int socket;
    char* filepath;
    atomic_int* tracker;
} SendFileArgs;
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

#endif