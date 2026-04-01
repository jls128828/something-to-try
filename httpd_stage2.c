#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define PORT 8080
#define BUFFER_SIZE 1048576
#define WEB_ROOT "./www"
#define MAX_EVENTS 1024
#define MAX_CONNECTIONS 1024

// 写日志
void write_log(const char *client_ip, const char *method, const char *path, int status) {
    FILE *log = fopen("access.log", "a");
    if (!log) return;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);
    fprintf(log, "%s - %s - %s %s - %d\n", time_str, client_ip, method, path, status);
    fclose(log);
}

// 读取文件内容
int read_file(const char *path, char *buffer, size_t buffer_size) {
    FILE *file = fopen(path, "rb");
    if (!file) return -1;
    fseek(file, 0, SEEK_END);
    long actual_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (actual_size >= buffer_size) {
        printf("Warning: file too large, truncated\n");
        actual_size = buffer_size - 1;
    }
    size_t bytes_read = fread(buffer, 1, actual_size, file);
    fclose(file);
    buffer[bytes_read] = '\0';
    return bytes_read;
}

// 根据扩展名返回 MIME 类型
const char *get_mime_type(const char *path) {
    if (path == NULL) return "text/plain";
    const char *ext = strrchr(path, '.');
    if (!ext) return "text/plain";
    if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0)
        return "text/html";
    if (strcasecmp(ext, ".css") == 0)
        return "text/css";
    if (strcasecmp(ext, ".js") == 0)
        return "application/javascript";
    if (strcasecmp(ext, ".png") == 0)
        return "image/png";
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    if (strcasecmp(ext, ".ico") == 0)
        return "image/x-icon";
    if (strcasecmp(ext, ".gif") == 0)
        return "image/gif";
    return "text/plain";
}

// 解析请求，获取路径
void parse_request(const char *request, char *path, size_t path_size) {
    char method[16], http_version[16];
    sscanf(request, "%s %s %s", method, path, http_version);
    if (strcmp(path, "/") == 0) {
        strcpy(path, "/index.html");
    }
}

// 构造 404 响应
void build_404_response(char *response, size_t *resp_len) {
    const char *body =
        "<html><head><title>404 Not Found</title></head>"
        "<body><h1>404 Not Found</h1><p>What page you find is not exist!</p></body></html>";
    int len = sprintf(response,
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %ld\r\n"
        "\r\n"
        "%s",
        strlen(body), body);
    if (resp_len != NULL) {
        *resp_len = len;
    }
}

// 循环发送所有数据
int send_all(int sock, const char *data, int len) {
    int total_sent = 0;
    while (total_sent < len) {
        int sent = send(sock, data + total_sent, len - total_sent, 0);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            perror("send failed");
            return -1;
        }
        total_sent += sent;
    }
    return total_sent;
}

// 处理单个 HTTP 请求（非阻塞）
void handle_request(int client_fd, const char *buffer) {
    char request_path[128];
    char file_path[256];
    char file_content[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    parse_request(buffer, request_path, sizeof(request_path));
    
    // 获取客户端 IP 和请求方法
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    getpeername(client_fd, (struct sockaddr *)&addr, &addrlen);
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, client_ip, sizeof(client_ip));
    
    char method[16], http_version[16], original_path[128];
    sscanf(buffer, "%s %s %s", method, original_path, http_version);
    
    snprintf(file_path, sizeof(file_path), "%s%s", WEB_ROOT, request_path);
    
    int file_size = read_file(file_path, file_content, BUFFER_SIZE);
    if (file_size >= 0) {
        const char *mime = get_mime_type(file_path);
        if (mime == NULL) mime = "text/plain";
        
        int header_len = snprintf(response, sizeof(response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %d\r\n"
            "\r\n",
            mime, file_size);
        send_all(client_fd, response, header_len);
        send_all(client_fd, file_content, file_size);
        
        write_log(client_ip, method, request_path, 200);
    } else {
        build_404_response(response, NULL);
        send_all(client_fd, response, strlen(response));
        write_log(client_ip, method, request_path, 404);
    }
    
    shutdown(client_fd, SHUT_WR);
}

// 设置 socket 为非阻塞
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    printf("Starting HTTP server with epoll...\n");
    
    int server_fd, epoll_fd;
    struct sockaddr_in address;
    int opt = 1;
    
    mkdir(WEB_ROOT, 0755);
    
    // 创建 socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    // 设置端口重用
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }
    
    // 设置非阻塞
    if (set_nonblocking(server_fd) < 0) {
        perror("set_nonblocking failed");
        exit(EXIT_FAILURE);
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }
    
    // 创建 epoll 实例
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1 failed");
        exit(EXIT_FAILURE);
    }
    
    // 将 server_fd 加入 epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
        perror("epoll_ctl: server_fd");
        exit(EXIT_FAILURE);
    }
    
    printf("HTTP server (epoll) is running on port %d...\n", PORT);
    printf("Visit http://192.168.132.128:%d\n", PORT);
    
    struct epoll_event events[MAX_EVENTS];
    char buffer[BUFFER_SIZE];
    
    while (1) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            
            if (fd == server_fd) {
                // 新连接
                while (1) {
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        perror("accept failed");
                        break;
                    }
                    
                    // 设置客户端 socket 非阻塞
                    set_nonblocking(client_fd);
                    
                    // 加入 epoll
                    ev.events = EPOLLIN | EPOLLET;  // 边沿触发
                    ev.data.fd = client_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
                        perror("epoll_ctl: client_fd");
                        close(client_fd);
                        continue;
                    }
                    
                    printf("New connection from %s:%d\n", 
                           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                }
            } else {
                // 客户端有数据可读
                memset(buffer, 0, BUFFER_SIZE);
                int valread = read(fd, buffer, BUFFER_SIZE - 1);
                
                if (valread < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                    perror("read failed");
                    close(fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                } else if (valread == 0) {
                    // 客户端关闭连接
                    printf("Client disconnected\n");
                    close(fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                } else {
                    // 处理请求
                    buffer[valread] = '\0';
                    printf("\nReceived request:\n%s\n", buffer);
                    handle_request(fd, buffer);
                    
                    // 短连接，处理完就关闭（从 epoll 移除并 close）
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                }
            }
        }
    }
    
    close(server_fd);
    close(epoll_fd);
    return 0;
}
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       
