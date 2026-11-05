#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <ctype.h>
#include "network_client.h"


int connect_to_server(struct sockaddr_in server_addr){
    int connect_client_fd;
    if((connect_client_fd=socket(AF_INET,SOCK_STREAM,0))<0){
        printf("[-] Error when creating the socket \n");
        exit(EXIT_FAILURE);
    }    
        
    if(connect(connect_client_fd,(struct sockaddr*)&server_addr,sizeof(server_addr))<0){
        printf("[-] Could not connect to the server through the socket \n");
        exit(EXIT_FAILURE);
    }
    return connect_client_fd;
}

char* recv_from_server(int connect_client_fd, char buffer_out[1024]){
    recv(connect_client_fd,&buffer_out,strlen(buffer_out)-1,0);
    return convert_to_uppercase(buffer_out);    
}

char* convert_to_uppercase(char input[1024]){
    for(int i=0;input[i]!='\0';i++){
        input[i]=(char)toupper(input[i]);
    }
    return input;
}
