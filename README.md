#HTTP Server in C
A simple HTTP/1.1 server implemented in C on Linux. Supports static files (HTML, images), 404 error pages, and access logging.

##Features
- Parse HTTP GET requests
- Serve static files (HTML, JPEG, PNG, etc.) with correct MIME types
- 404 Not Found error page
- Access log recording (IP, method, path, status code)
- Handles large files (configurable buffer size)
- Multi-client support (each connection handled sequentially)
 
##Requirements
- Linux (tested on CentOS 7)
- GCC compiler
- Make (optional)
 
##Compile and Run
```bash
gcc httpd_stage2.c -o httpd_stage2
./httpd_stage2
