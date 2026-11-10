#define BUFFER_SIZE 1024
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <string.h>
#include <arpa/inet.h>
#include<ctype.h>
#include <pthread.h>

#include "cjson/cJSON.h"
#include "player.h"
#include "network_client.h"
#include "comm.h"

static Player player;
static pthread_mutex_t clients_lock=PTHREAD_MUTEX_INITIALIZER;
static char *prompt="Awale (not registered)";
static char buffer_in[1024];

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
    cJSON *games=cJSON_GetObjectItemCaseSensitive(item,"Players");
    if(cJSON_IsArray(games)){
        cJSON *game;
        printf("\n----------Ongoing Games------------\n");
        cJSON_ArrayForEach(game,games){
            cJSON* game_ID=cJSON_GetObjectItemCaseSensitive(game,"ID");
            printf("%d \n",game_ID->valueint);
        }
    }
}
void *receive_msg(void *arg){
    int connect_client_fd=*(int *)arg;
    char *msg_content;
    char username[256];
    uint32_t msg_type,msg_length;

    while(1){
        recv(connect_client_fd,&msg_type,sizeof(msg_type),0);
        msg_type=ntohl(msg_type);
        printf("MSG_TYPE: %d \n",msg_type);
                        
        recv(connect_client_fd,&msg_length,sizeof(msg_length),0);        
        msg_length=ntohl(msg_length);
        printf("MSG_LENGTH: %d \n",msg_length);

        msg_content=malloc(msg_length+1);
        recv(connect_client_fd,msg_content,msg_length,0);
        msg_content[msg_length]='\0';                        

        switch(msg_type){
            case MSG_TEXT:{

                printf("Received response: %s \n",msg_content);

                if(strcmp(msg_content,"USERNAME_AVAILABLE")==0){
                    printf("Buffer_in in the thread: %s \n",buffer_in);
                    char *command=strtok(buffer_in," ");
                    char *username=strtok(NULL, " ");
                    printf("Username in the thread: %s \n",username);
                    pthread_mutex_lock(&clients_lock);
                    strncpy(player.username,username,strlen(username));                    
                    prompt=player.username;
                    pthread_mutex_unlock(&clients_lock);
                }
                if(strcmp(msg_content,"USERNAME_NOT_AVAILABLE")==0){
                    printf("[-] The requested username is already taken, please try again \n");
                }
                if(strcmp(msg_content,"BIO_ADDED")==0){
                    printf("[+] Bio added successfully \n");
                }
                if(strcmp(msg_content,"SERVER_DISCONNECTED")==0){
                    printf("[-] Server disconnected from the network \n");
                    close(connect_client_fd);
                }
                if(strcmp(msg_content,"INVALID_COMMAND")==0){
                    printf("[-] Server returned: invalid command \n");
                }


            break;
            }
            case MSG_JSON:{
                cJSON *json = cJSON_Parse(msg_content);
                if(json==NULL){
                    printf("[-] Invalid JSON \n");
                    return NULL;                
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
            
            break;    
            }
            case MSG_INVITE:
            {
                Invite invite;
                memcpy(&invite,msg_content,sizeof(invite));

                printf("[+] Receiving the invite from the server \n");

                //invite.sender_username[sizeof(invite.sender_username)-1]='\0';
                //printf("Sender: %s \n",invite.sender_username);
                printf("State: %d",invite.invite_state);                

                do{
                    int choice;
                    printf("\n-------------Invitation from Player %s------------\n",invite.sender_username);
                    printf("0: Accept\n");
                    printf("1: Refuse\n");
                    scanf("%d",&choice);
                    if(choice==0){
                        invite.invite_state=REFUSED;
                        char *response="REFUSED";
                        send(connect_client_fd,response,strlen(response),0);
                    }
                    else if(choice==1){
                        invite.invite_state=ACCEPTED;
                        char *response="ACCEPTED";
                        send(connect_client_fd,response,strlen(response),0);   
                    }
                    else{
                        invite.invite_state=PENDING;
                    }                    
                }while(invite.invite_state==PENDING);
                
            break;
            } 

        }                                              
    }
}

void main(int argc, char* argv[]){
    struct sockaddr_in server_addr;
    int connect_client_fd;
    char username[256];
    char *msg_content;
    uint32_t msg_type,msg_length;
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
    server_addr.sin_port=htons(atoi(argv[2]));

    printf("\n--- Connecting to the server ---\n");
    connect_client_fd=connect_to_server(server_addr);  

    int *arg=malloc(sizeof(int));    
    *arg=connect_client_fd;
    
    pthread_t tid;
    printf("[+] Creating a new thread \n");
    if(pthread_create(&tid,NULL,receive_msg,arg)!=0){
        printf("[-] Could not create the thread \n");
        close(connect_client_fd);
        free(arg);
    }
    else{
        pthread_detach(tid);            
    }

    printf("\n-------------------------------------\n");
    printf("Type /help for available commands");
    printf("\n-------------------------------------\n");

    do{
        pthread_mutex_lock(&clients_lock);
        if(player.username!=NULL){
            prompt=player.username;
        }
        pthread_mutex_unlock(&clients_lock);        
        
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
            printf("Sending \n");
            if(send(connect_client_fd,&buffer_in,strlen(buffer_in),0)<=0){
                printf("[-] Could not send data to the server \n");
                exit(EXIT_FAILURE);                
            }
            printf("Sent Buffer: %s \n",buffer_in);
        }                
    }while(strcmp(buffer_in,"/quit")!=0);
    
    close(connect_client_fd);
    printf("\n--------------------------------------\n");
    printf("[*] You left the server \n");
    printf("\n--------------------------------------\n");
    exit(EXIT_SUCCESS);   
}