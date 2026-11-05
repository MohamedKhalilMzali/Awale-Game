#define BUFFER_SIZE 1024
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <ctype.h>
#include "network_server.h"

int create_listener(){
    int listen_socket_fd;
    int opt=1;
    struct sockaddr_in socket_addr;

    if((listen_socket_fd=socket(AF_INET,SOCK_STREAM,0))<0){
        printf("[-] Error when creating the socket \n");
        exit(EXIT_FAILURE);
    }
    if(setsockopt(listen_socket_fd,SOL_SOCKET,SO_REUSEADDR|SO_REUSEPORT,&opt,sizeof(opt))<0){
        printf("[-] Error when trying to set the socket options \n");
        exit(EXIT_FAILURE);
    }
    socket_addr.sin_family=AF_INET;
    socket_addr.sin_port=htons(8000);
    socket_addr.sin_addr.s_addr=INADDR_ANY;
    if(bind(listen_socket_fd,(struct sockaddr*)&socket_addr,sizeof(socket_addr))<0){
        printf("[-] Could not bind the socket to a port \n");
        exit(EXIT_FAILURE);
    }
    if(listen(listen_socket_fd,5)<0){
        printf("[-] Could not listen to the incoming connection \n");
        exit(EXIT_FAILURE);
    }
    return listen_socket_fd;
}

void recv_from_client(int connect_socket_fd, char buffer_out[1024]){    
    ssize_t nbBytes=recv(connect_socket_fd,buffer_out,BUFFER_SIZE-1,0);
    buffer_out[nbBytes] = '\0';
}