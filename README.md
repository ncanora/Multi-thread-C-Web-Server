# Multi-thread-C-Web-Server
This is a Web Server in C I wrote as part of a project for my Distributed Systems course. It uses pthreads to handle multiple clients and supports favicons, images, and GIFs.

# HTTP Connections
The server supports HTTP/1.0 and HTTP/1.1 (keep-alive connections). Connections are closed if the server has not recieved a request by the client within a dynamically calculated heuristic. 

# Content
The server supports HTML and CSS (no javascript), JPEG/JPG, PNG, GIF, and favicons. The content should be placed in "root/content". You can change the default content directory in the web_lib.h file (default is root/content). Your home page should be named index.html for the web server, as when a request has no specified file it will search for "index.html" by default.

## Supported Response Codes
Currently the server only supports 200 (OK), 403 (Forbidden), 400 (Bad Request), and 404 (Not found). There are two required HTML files for these responses to be handled properly: "badrequest.html" for 400 Bad Request, "error.html" for 404 Not found and 400 (Bad request). 

# Use
1. Clone the repository and create a new folder called "content" in the root of the repository.
2. Add your content files including the required "index.html", "badrequest.html", and "error.html"
3. Compile using the Makefile and run the executable with or without optional flags. Default port is 8080 and default document root is the CWD: web_server -port <portno> -document_root <path>

# Logging and Testing
The web server tracks the current number of clients connected and the number of threads used (should be the same) which are periodically printed out to the console. The server has only been tested on a campus linux server, and typically crashes after about ~1000 clients have connected. 

# Timeout
A timeout window is calculated using an atomic integer that tracks the number of threads in use. This is plugged into an exponential decay function and determines the keep-alive window of the current client's connection. The base (or max timeout window) can be changed in web_server.c. 
