#define BUFFER_SIZE 1024
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <string.h>
#include <arpa/inet.h>
#include "cjson/cJSON.h"
#include "player.h"
#include<ctype.h>
#include "network_client.h"

static Player player;


void display_banner(){
    printf("\n------------------------------------------------\n");
    printf("\n-----------------AWALE GAME---------------------\n");
    printf("\n------------------------------------------------\n");
}

void display_menu(){
    printf("/register [username] \n");
    printf("/add-bio [username] [bio] \n");
    printf("/list-bio [username] \n");
    printf("/invite [username] \n");
    printf("/list-online-users \n");
    printf("/list-current-games \n");
    printf("/quit \n");
}

void main(int argc, char* argv[]){
    struct sockaddr_in server_addr;
    int connect_client_fd;
    char buffer_in[1024],buffer_out[1024];
    char username[256];
    char *prompt="Awale (not registered)";
    int nbBytes, server_port;

     if(argc<2){
        printf("Usage: %s <IP_ADDRESS> <PORT>\n",argv[0]);
        exit(EXIT_FAILURE);
    }
    
    if((inet_pton(AF_INET,argv[1],&server_addr.sin_addr))<1){
        printf("[-] Invalid IP address \n");
        exit(EXIT_FAILURE);
    }
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(8000);

    printf("\n--- Connecting to the server --- \n");
    connect_client_fd=connect_to_server(server_addr);  

    printf("\n-------------------------------------\n");
    printf("Type /help for available commands");
    printf("\n-------------------------------------\n");

    do{      
        printf("%s> ",prompt);
        fflush(stdout);
        
        fgets(buffer_in,sizeof(buffer_in),stdin);
        buffer_in[sizeof(buffer_in)-1]='\0';
        buffer_in[strcspn(buffer_in,"\n")]='\0';
        
        if(strcmp(buffer_in,"/help")==0){
            display_menu();
            continue;
        }
        else{
            if(send(connect_client_fd,&buffer_in,strlen(buffer_in),0)<=0){
                printf("[-] Could not send data to the server \n");
                exit(EXIT_FAILURE);                
            }
            printf("Sent Buffer: %s \n",buffer_in);
        }

        int nbBytes=recv(connect_client_fd,buffer_out,sizeof(buffer_out),0);        
        if(nbBytes>0){
            printf("nbBytes: %d \n",nbBytes);   
        }
        if(strcmp(buffer_out,"USERNAME_AVAILABLE")==0){
            char *command=strtok(buffer_in," ");
            char *username=strtok(NULL, " ");
            strncpy(player.username,username,strlen(username));
            prompt=player.username;
        }
        if(strcmp(buffer_out,"BIO_ADDED")==0){
            printf("[+] Bio added successfully \n");
        }
        if(strcmp(buffer_out,"SERVER_DISCONNECTED")==0){
            break;
        }                                      

    } while(strcmp(buffer_in,"/quit")!=0);

    close(connect_client_fd);
    printf("\n--------------------------------------\n");
    printf("[*] You left the server \n");
    printf("\n--------------------------------------\n");
    exit(EXIT_SUCCESS);   
}
