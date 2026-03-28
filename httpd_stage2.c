#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define PORT 8080
#define BUFFER_SIZE 1048576 
#define WEB_ROOT "./www"

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
    
    // 获取文件实际大小（用于调试）
    fseek(file, 0, SEEK_END);
    long actual_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    printf("DEBUG - 文件实际大小: %ld 字节\n", actual_size);
    
    size_t bytes_read = fread(buffer, 1, buffer_size - 1, file);
    printf("DEBUG - fread 读取了: %zu 字节\n", bytes_read);
    
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
    printf("Preparing to send %d bytes.\n", len);
    while (total_sent < len) {
        int sent = send(sock, data + total_sent, len - total_sent, 0);
        if (sent < 0) {
            perror("send failed");
            return -1;
        }
        total_sent += sent;
        printf("Have sent %d bytes, accelerating %d / %d\n", sent, total_sent, len);
    }
    printf("=== send_all has accomplished, in fact, sent %d bytes===\n", total_sent);
    return total_sent;
}

int main() {
    printf("Programming is beginning...\n");
    fflush(stdout);

    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};
    char file_path[256];
    char file_content[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    mkdir(WEB_ROOT, 0755);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("HTTP is opening, listening port %d...\n", PORT);
    printf("Please visit it in: http://192.168.132.128:%d\n", PORT);
    printf("Page is under %s content\n", WEB_ROOT);

    while (1) {
        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) {
            perror("accept failed");
            continue;
        }

        int valread = read(client_fd, buffer, BUFFER_SIZE - 1);
        if (valread > 0) {
            buffer[valread] = '\0';
            printf("\nReceived request:\n%s\n", buffer);

            // 解析请求路径（会处理 / 转换为 /index.html）
            char request_path[128];
            parse_request(buffer, request_path, sizeof(request_path));
            printf("Received file: %s\n", request_path);

            // 获取客户端 IP 和请求方法（用单独的变量，不要覆盖 request_path）
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &address.sin_addr, client_ip, sizeof(client_ip));
            char method[16], http_version[16];
            char original_path[128];  // 新增：存放原始路径，不覆盖 request_path
            sscanf(buffer, "%s %s %s", method, original_path, http_version);

            // 拼接完整文件路径
            snprintf(file_path, sizeof(file_path), "%s%s", WEB_ROOT, request_path);
            printf("The true path: %s\n", file_path);

            int file_size = read_file(file_path, file_content, BUFFER_SIZE);
            if (file_size >= 0) {
                const char *mime = get_mime_type(file_path);
                printf("DEBUG - mime type: %s\n", mime);
                if (mime == NULL) mime = "text/plain";

                // 发送 HTTP 头部
                int header_len = snprintf(response, sizeof(response),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: %s\r\n"
                    "Content-Length: %d\r\n"
                    "\r\n",
                    mime, file_size);
                send_all(client_fd, response, header_len);

                // 发送文件内容
                send_all(client_fd, file_content, file_size);

                // 确保数据发送完毕
                shutdown(client_fd, SHUT_WR);

                // 写日志
                write_log(client_ip, method, request_path, 200);
            } else {
                build_404_response(response, NULL);
                send_all(client_fd, response, strlen(response));
                write_log(client_ip, method, request_path, 404);
            }
        }
        close(client_fd);
        memset(buffer, 0, BUFFER_SIZE);
    }

    close(server_fd);
    return 0;
}                                 
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    
