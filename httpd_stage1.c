#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define PORT 8080
#define BUFFER_SIZE 1024
int main(){
    int server_fd,client_fd;
    struct sockaddr_in address;
    int opt=1;
    int addrlen=sizeof(address);
    char buffer[BUFFER_SIZE]={0};
    if((server_fd=socket(AF_INET,SOCK_STREAM,0))==0){
       perror("socket failed");
       exit(EXIT_FAILURE);}
    if(setsockopt(server_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt))){
       perror("setsockopt failed");
       exit(EXIT_FAILURE);}
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=INADDR_ANY;
    address.sin_port=htons(PORT);
    if(bind(server_fd,(struct sockaddr *)&address,sizeof(address))<0){
       perror("bind failed");
       exit(EXIT_FAILURE);}
    if (listen(server_fd, 3) < 0) {
    perror("listen failed");
    exit(EXIT_FAILURE);}
    printf("HTTP has played,the port is %d...\n",PORT);
    printf("Please visit it in the browser:http://192.168.132.128:%d\n",PORT);
    while(1){
          if((client_fd=accept(server_fd,(struct sockaddr *)&address,
                               (socklen_t*)&addrlen))<0){
              perror("accept failed");
              continue;}
    int valread=read(client_fd,buffer,BUFFER_SIZE-1);
    if(valread>0){
       printf("Accept request:%s\n",buffer);}
    char *response=
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 12\r\n"
        "\r\n"
        "Hello World!";
    send(client_fd,response,strlen(response),0);
    close(client_fd);
    memset(buffer,0,BUFFER_SIZE);}
    close(server_fd);
    return 0;}

