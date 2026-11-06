#define BUFFER_SIZE 1024
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <string.h>
#include <arpa/inet.h>
#include<ctype.h>

#include "cjson/cJSON.h"
#include "player.h"
#include "network_client.h"
#include "comm.h"

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

void display_players_from_json(cJSON *item){
    cJSON *players=cJSON_GetObjectItemCaseSensitive(item,"Players");
    if(cJSON_IsArray(players)){
        cJSON *player;
        printf("\n----------Online Users------------\n");
        cJSON_ArrayForEach(player,players){
            cJSON* username=cJSON_GetObjectItemCaseSensitive(player,"username");
            printf("%s \n",username->valuestring);
        }
    }
}

void display_games_from_json(cJSON *item){

}

void main(int argc, char* argv[]){
    struct sockaddr_in server_addr;
    int connect_client_fd;
    char buffer_in[1024];
    char *msg_content;
    uint32_t msg_type,msg_length;
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

    printf("\n--- Connecting to the server ---\n");
    connect_client_fd=connect_to_server(server_addr);  

    printf("\n-------------------------------------\n");
    printf("Type /help for available commands");
    printf("\n-------------------------------------\n");

    do{
        if(player.username!=NULL){
            prompt=player.username;
        }        
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

        recv(connect_client_fd,&msg_type,sizeof(msg_type),0);        
        recv(connect_client_fd,&msg_length,sizeof(msg_length),0);
        
        msg_type=ntohl(msg_type);
        msg_length=ntohl(msg_length);
        
        msg_content=malloc(msg_length+1);
        
        recv(connect_client_fd,msg_content,msg_length,0);
        msg_content[msg_length]='\0';

        switch(msg_type){
            case MSG_TEXT:{

                printf("Received response: %s \n",msg_content);

                if(strcmp(msg_content,"USERNAME_AVAILABLE")==0){
                    char *command=strtok(buffer_in," ");
                    char *username=strtok(NULL, " ");
                    strncpy(player.username,username,strlen(username));                    
                    prompt=player.username;
                }
                if(strcmp(msg_content,"USERNAME_NOT_AVAILABLE")==0){
                    printf("[-] The requested username is already taken, please try again \n");
                }
                if(strcmp(msg_content,"BIO_ADDED")==0){
                    printf("[+] Bio added successfully \n");
                }
                if(strcmp(msg_content,"SERVER_DISCONNECTED")==0){
                    break;
                }

            break;
            }
            case MSG_JSON:{
                cJSON *json = cJSON_Parse(msg_content);
                if(json==NULL){
                    printf("[-] Invalid JSON \n");
                    return;                
                }          
                cJSON *item=json->child;
                if(strcmp(item->string,"Players")==0){
                    printf("Correct \n");
                    cJSON_Print(json);
                    display_players_from_json(json);
                }
                if(strcmp(item->string,"Games")==0){                    
                    display_games_from_json(json);
                }    
            

            }
        }                                              
    } while(strcmp(buffer_in,"/quit")!=0);

    close(connect_client_fd);
    printf("\n--------------------------------------\n");
    printf("[*] You left the server \n");
    printf("\n--------------------------------------\n");
    exit(EXIT_SUCCESS);   
}
