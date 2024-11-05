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
#include <limits.h>
#include "web_lib.h"                       // header file for library

#define TIMEOUT_BASE (time_t) 3            // Initial value for client conn timeout (decays by get_http_timeout as thread count increases)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// we need to define this globally to access this memory location for all threads
atomic_int tracker = 0; // num of threads

// function used to calculate max timeout (exponentially decreases as thread count increases)
time_t get_http_timeout(atomic_int thread_count) {
    if (thread_count <= (atomic_int)1) {
        return TIMEOUT_BASE;
    }
    return (time_t)(TIMEOUT_BASE*exp(-.01*((time_t)thread_count)));
}

void* handle_request(void* arg) {
    // cast and dereference socket and thread_tracker
    ThreadArgs* args = (ThreadArgs*)arg;
    int connfd = args->socket;
    atomic_int* thread_tracker = args->thread_tracker;
    atomic_fetch_add(thread_tracker, 1);

    char buffer[HTTP_BUF_SIZE];
    int rec_bytes;
    pthread_t file_tid;
    void* filesend_thread_result;

    fd_set readfds;                     // File descriptor set for select
    struct timeval timeout;
    int bad_request_counter = 0;

    SendFileArgs* file_args = malloc(sizeof(SendFileArgs));
    file_args->filepath = malloc(MAX_LEN_FILE_PATH);
    file_args->socket = connfd;
    file_args->tracker = thread_tracker;


    while (bad_request_counter < 10) {      // if a client is spamming bad requests we close the connection
        FD_ZERO(&readfds);                  // Clear the set
        FD_SET(connfd, &readfds);           // Add the socket to the set
        memset(buffer, 0, sizeof(buffer));  // Clear buffer
        timeout.tv_sec = get_http_timeout(*thread_tracker);
        timeout.tv_usec = 0;

        // wait for data on the socket
        int activity = select(connfd + 1, &readfds, NULL, NULL, &timeout);
        if (activity == 0) {                // no message sent within timeout window
            printf("Client socket timed out. Closing connection.\n");
            close(connfd);
            atomic_fetch_sub(thread_tracker, 1);
            free(arg);
            return NULL;
        } else if (activity < 0) {          // Error occurred
            perror("select() error");
            break;
        }

        if (FD_ISSET(connfd, &readfds)) {  // Request sent
            rec_bytes = recv(connfd, buffer, sizeof(buffer) - 1, 0);

            if (rec_bytes <= 0) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    printf("Connection timed out for client %d\n", connfd);
                } else {
                    perror("Error reading from socket");
                }
                break;
            }

            buffer[rec_bytes] = '\0';       // terminate buffer in case missing null char
            printf("Received request:\n%s\n", buffer);
            
            // Parse request into struct
            HttpRequest* request = parse_http_request(buffer);
            if (request == NULL) {
                bad_request_counter++;
                strcpy(request->http_method, "BADREQUEST"); // will cause jump to badrequest
            }

            char http_response[HTTP_BUF_SIZE];
            size_t file_content_size;
            
            if (strcmp(request->http_method, "GET") == 0) {       // Only support HTTP GET, any other methods return bad request
                char* file_path = find_filepath(request->file_path);

                if (file_path == NULL) {                         // If file not found
                    file_path = find_filepath("error.html");
                    file_content_size = get_file_size(file_path);
                    strcpy(http_response, build_http_response_header(file_content_size, file_path, 404, request->http_protocol, request->keep_alive, request->hostname));
                    send(connfd, http_response, strlen(http_response), MSG_NOSIGNAL);
                    printf("Response:\n%s\n", http_response);

                    file_args->filepath = file_path;

                    /*
                    if (pthread_create(&file_tid, NULL, send_file, file_args) != 0) {
                        perror("Error creating thread");
                        break;
                    }

                    pthread_join(file_tid, filesend_thread_result);
                    */
                   send_file(file_args);
                    // check if should keep connection open, if not break loop
                    if (leave_conn_open(connfd, request)) {
                        printf("Leaving connection open!\n");
                        continue;
                    }
                    printf("Breaking connection, http 1.0 or close\n");
                    break;

                } else {
                    file_content_size = get_file_size(file_path);
                    strcpy(http_response, build_http_response_header(file_content_size, request->file_path, 200, request->http_protocol, request->keep_alive, request->hostname));
                    send(connfd, http_response, strlen(http_response), MSG_NOSIGNAL);

                    printf("Response:\n%s\n", http_response);
                    
                    file_args->filepath = file_path;;

                    /*
                    if (pthread_create(&file_tid, NULL, send_file, file_args) != 0) {
                        perror("Error creating thread");
                        break;
                    }

                    pthread_join(file_tid, filesend_thread_result);
                    */
                   send_file(file_args);
                    // check if should keep connection open, if not break loop
                    if (leave_conn_open(connfd, request)) {
                        printf("Leaving connection open!\n");
                        continue;
                    }
                    printf("Breaking connection, http 1.0 or close\n");
                    break;
                }

            } else {
                // Handle unsupported methods
                printf("Bad/unformatted request\n");
                char* file_path = find_filepath("badrequest.html");
                file_content_size = get_file_size(file_path);
                strcpy(http_response, build_http_response_header(file_content_size, "badrequest.html", 400, request->http_protocol, request->keep_alive, request->hostname));
                send(connfd, http_response, strlen(http_response), MSG_NOSIGNAL);
                
                printf("Response:\n%s\n", http_response);
                
                file_args->filepath = file_path;;

                /*
                if (pthread_create(&file_tid, NULL, send_file, file_args) != 0) {
                    perror("Error creating thread");
                    break;
                }

                pthread_join(file_tid, filesend_thread_result);
;               */
                send_file(file_args);
                free(request);
                // check if should keep connection open, if not break loop
                if (leave_conn_open(connfd, request)) {
                    printf("Leaving connection open!\n");
                    continue;
                }
                break;
            }
        }
    }
    atomic_fetch_sub(thread_tracker, 1);
    free(arg);
    free(file_args);
    close(connfd);          // Close the connection
    return NULL;
}

int main(int argc, char* args[]) {
    char document_root[PATH_MAX];  // Defaultroot
    int port = 8080;                   // Default port

    if (getcwd(document_root, sizeof(document_root)) == NULL) {
        perror("Error getting current directory");
        return -1;
    }
    // Parse command line args
    for (int i = 1; i < argc; i++) {
        if (strcmp(args[i], "-document_root") == 0) {
            if (i + 1 < argc) {
                strcpy(document_root, args[i+1]);
                i++; 
            } else {
                fprintf(stderr, "Error: No document root provided after -document_root.\n");
                return 1;
            }
        } else if (strcmp(args[i], "-port") == 0) {
            if (i + 1 < argc) {
                port = atoi(args[i + 1]);
                i++;
            } else {
                fprintf(stderr, "Error: No port number provided after -port.\n");
                return 1;
            }
        }
    }

    // Change to the document root directory
    if (chdir(document_root) != 0) {
        perror("Error changing directory");
        return 1;
    }

    socklen_t clientlen;
    struct sockaddr_in server_addr;
    int sock = bind_server_setup(port, INADDR_ANY, &server_addr);

    // Check if successful binding occurred
    if (sock < 0) {
        printf("Failed to bind to port\n");
        return -1;
    }
    
    // Verify listening
    if (listen(sock, SOMAXCONN) < 0) {
        perror("Listen failed");
        close(sock);
        return -1;
    }
    
    // Start listening for clients
    struct sockaddr clientaddr;
    pthread_t tid;

    // Loop 
    while (1) {
        clientlen = sizeof(clientaddr);
        int* connfd = malloc(sizeof(int));
        *connfd = accept(sock, (struct sockaddr *)&clientaddr, &clientlen);

        if (*connfd < 0) {
            perror("ERROR on accept");
            free(connfd);
            continue; // Continue accepting other connections
        }

        printf("Client connected!\n");

        // Build struct for thread args (including atomic int thread count tracker for safe access)
        ThreadArgs* args = malloc(sizeof(ThreadArgs));
        args->socket = *connfd;
        args->thread_tracker = &tracker;

        // Create a thread to handle the connection
        if (pthread_create(&tid, NULL, handle_request, args) != 0) {
            perror("Error creating thread");
            close(*connfd);
            free(connfd);
        } else {
            pthread_detach(tid);
            printf("\nThreads currently in use: %d\n", atomic_load(args->thread_tracker));
        }
    }

    close(sock);
    return 0;
}