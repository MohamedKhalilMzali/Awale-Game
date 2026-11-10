#define MAX_PLAYERS 100
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>

#include "player.h"
#include "cjson/cJSON.h"
#include "network_server.h"

static Player player_c[MAX_PLAYERS];
static int player_count=0;
static pthread_mutex_t players_lock=PTHREAD_MUTEX_INITIALIZER; 

cJSON* write_players_to_json(cJSON* players_json, cJSON* players_array, char *username){    
    cJSON *player=cJSON_CreateObject();
    cJSON_AddStringToObject(player,"username",username);
    cJSON_AddItemToArray(players_array,player);
    printf("[JSON] Added the username to the array \n");
    
    return players_json;
}

int username_exists(const char *username)
{
    int exists=0;
    if(player_count>0){
        for(int i=0;i<player_count;i++){           
            if( (strcmp(player_c[i].username,username)) == 0 ){
                exists=1;
                break;
            }
        }
    }
    return exists;
}

char* register_username(int connection_socket, const char *username)
{
    if(username==NULL){
        return NULL;
    }
    
    if(username_exists(username)!=0){
        return "USERNAME_NOT_AVAILABLE";
    }
    if(player_count>=MAX_PLAYERS){        
        return NULL;
    }

    Player player={0} ;       
    snprintf(player.username,sizeof(player.username),"%s",username);
    player.state=ONLINE_IDLE;
    player.socket=connection_socket;

    printf("[+] Added socket %d to the user %s \n",player.socket,player.username);

    player_c[player_count]=player;
    player_count++; 
    
    return "USERNAME_AVAILABLE";
}

char* search_player_by_socket(int player_socket){
    for(int i=0;i<player_count;i++){
        if(player_c[i].socket==player_socket){       
            return player_c[i].username;
        }
    }
    return NULL;
}

Player* search_player(const char* username){
    if(username==NULL){
        return NULL;
    }
    if(player_count>0){
        for(int i=0;i<player_count;i++){
            if( (strcmp(player_c[i].username,username)) == 0){       
                return &player_c[i];
            }
        }
    }
    return NULL;
}

int search_player_index(const char* username){
    if(username!=NULL){
        if(player_count>0){
            for(int i=0;i<player_count;i++){
                if( (strcmp(player_c[i].username,username)) == 0){       
                    return i;
                }
            }
        }
    }
    return -1;
}

void display_player_info(const char* username){
    Player* player=search_player(username);
    char *state;
    if(player!=NULL){
        printf("\n------------------------\n");
        printf("Username: %s \n",player->username);
        printf("Bio: %s \n",player->bio);
        switch(player->state){
            case OFFLINE:
            {
                state="OFFLINE";               
                break;
            }
            case ONLINE_IDLE:
            {
                state="ONLINE_IDLE";   
                break;
            }
            case ONLINE_IN_GAME:
            {
                state="ONLINE_IN_GAME";
                break;
            }
        }
        printf("State: %s \n",state);
        printf("\n------------------------\n");
    }
}

cJSON* display_online_players(){
    cJSON *online_users=NULL;
    cJSON *players_json=cJSON_CreateObject();
    cJSON *players_array=cJSON_AddArrayToObject(players_json,"Players");
    char *json=NULL;
    
    if(player_count>0){
        printf("Player count: %d \n",player_count);
        for(int i=0;i<player_count;i++){
            if(player_c[i].state==ONLINE_IDLE){
                printf("Username: %s\n",player_c[i].username);                
                online_users=write_players_to_json(players_json,players_array,player_c[i].username);                
            }
        }                        
    }
    return players_json;
}

int modify_user_state(const char *username,State state){
    int player_index=search_player_index(username);
    if(player_index!=-1){
        player_c[player_index].state=state;
        return 1;
    }
    return 0;
}   


void add_player_bio(const char *username, const char *bio){
    int player_index=search_player_index(username);
    if(player_index!=-1){
        snprintf(player_c[player_index].bio,sizeof(player_c[player_index].bio),"%s",bio);
    }
}

void send_invite(const char *sender_username, const char *receiver_username){
    Player* sender=search_player(sender_username);
    Player* receiver=search_player(receiver_username);

    if(sender==NULL){
        printf("[-] User %s does not exist \n",sender->username);
        return;
    }
    if(receiver==NULL){
         printf("[-] User %s does not exist \n",receiver->username);
        return;
    }

    printf("Sender [%s] socket %d\n",sender->username,sender->socket);
    printf("Receiver [%s] socket %d\n",receiver->username,receiver->socket);

    Invite invite;
    memcpy(&invite.sender_username,sender->username,sizeof(invite.sender_username));
    invite.invite_state=PENDING;

    if (fcntl(receiver->socket, F_GETFD) == -1) {
        perror("Receiver socket is invalid");
    }
    send_to_client(receiver->socket,MSG_INVITE,&invite,sizeof(invite));
    printf("[+] Sending invite from %s to %s on socket %d \n",sender->username,receiver->username,receiver->socket);
    
    /*char buffer_out[256];
    recv_from_client(receiver->socket,buffer_out);
    if(strcmp(buffer_out,"REFUSED")==0){        
        send_to_client(sender->socket,MSG_TEXT,&buffer_out,strlen(buffer_out));
    }
    if(strcmp(buffer_out,"ACCEPTED")==0){
        //running play_game function which launches the game between both players
    }
    */
    }


void *handle_client(void *arg){   
    char *save_ptr=NULL;
    char buffer_in[1024],buffer_out[1024];
    char username[256];
    ssize_t size_buffer; 
    
    int connect_socket_fd=*(int *)arg;
    free(arg);


    while(1){                                
        
        ssize_t n = recv_from_client(connect_socket_fd, buffer_out);

        if (n == 0) {
            printf("Client disconnected.\n");
            break;
        }

        if (n < 0) {
            perror("recv");
            break;
        }

        printf("Received: %s\n", buffer_out);

        char *save_ptr = NULL;
        char *command = strtok_r(buffer_out, " ", &save_ptr);

        if (command == NULL) {
            continue;
        }
        
        
      
            
        if(strcmp(command,"/register")==0){
            char *username=strtok_r(NULL," ",&save_ptr);
            printf("Username: %s \n",username);
            
            if(username==NULL){
                printf("Usage: /register [username] \n");
            }
            else{
                pthread_mutex_lock(&players_lock);
                char* msg=register_username(connect_socket_fd,username);   
                pthread_mutex_unlock(&players_lock);                    

                printf("Message from registration: %s \n",msg);    
                send_to_client(connect_socket_fd,MSG_TEXT,msg,strlen(msg));                                             
            }            
        }

        else if(strcmp(command,"/list-online-users")==0){
            pthread_mutex_lock(&players_lock);
            cJSON* onlinePlayers_json=display_online_players();         
            pthread_mutex_unlock(&players_lock);                         
            char* online_p=cJSON_PrintUnformatted(onlinePlayers_json);                                
            send_to_client(connect_socket_fd,MSG_JSON,online_p,strlen(online_p));                        
        }

        else if(strcmp(command,"/add-bio")==0){
            char *username=search_player_by_socket(connect_socket_fd);                
            char *bio=strtok_r(NULL, "\r\n",&save_ptr);

            printf("[*] Username: %s\n",username);
            printf("[*] Bio: %s\n",bio);
            
            pthread_mutex_lock(&players_lock);
            add_player_bio(username,bio);     
            pthread_mutex_unlock(&players_lock);           
            
            char *response="BIO_ADDED";
            send_to_client(connect_socket_fd,MSG_TEXT,response,strlen(response));
        }

        else if(strcmp(command,"/invite")==0){
            pthread_mutex_lock(&players_lock);
            char *sender_username=search_player_by_socket(connect_socket_fd);
            char *receiver_username=strtok_r(NULL, " ",&save_ptr);
            
            /*for (int i = 0; i < player_count; i++) {
                printf("Player: %s | Socket: %d\n",
                    player_c[i].username,
                    player_c[i].socket);
            }*/
            send_invite(sender_username,receiver_username);
            pthread_mutex_unlock(&players_lock); 
        }

        else if(strcmp(command,"/quit")==0){
            char *username=search_player_by_socket(connect_socket_fd);                
            modify_user_state(username,OFFLINE);
            close(connect_socket_fd);     
        }
        else{
            printf("[-] Invalid command from client: %s\n",buffer_out);
            char *response="INVALID_COMMAND";
            send_to_client(connect_socket_fd,MSG_TEXT,"INVALID_COMMAND",sizeof("INVALID_COMMAND"));
        }                                                    
    }                
}

void main(int argc, char* argv[]){
    int listen_socket_fd, connect_socket_fd;
    struct sockaddr_in socket_addr, remote_addr;

    if(argc<2){
        printf("Usage: %s <PORT>\n",argv[0]);
        exit(EXIT_FAILURE);
    }
    int port=atoi(argv[1]);
    printf("Received port: %d \n",port);
    printf("[SOCKET] Setting-up the listener \n");
    listen_socket_fd=create_listener(port);
    
    while(1){     
        socklen_t remote_addr_len=sizeof(remote_addr);   
        connect_socket_fd=accept(listen_socket_fd,(struct sockaddr*)&remote_addr,&remote_addr_len);
        
        if(connect_socket_fd<0){
            printf("[-] Could not accept the incoming connection from the client \n");
            continue;        
        }

        int *arg=malloc(sizeof(int));
        if(arg==NULL){
            close(connect_socket_fd);
            continue;
        }
        *arg=connect_socket_fd;
        
        pthread_t tid;
        if(pthread_create(&tid,NULL,handle_client,arg)!=0){
            printf("[-] Could not create the thread \n");
            close(connect_socket_fd);
            free(arg);
        }
        else{
            pthread_detach(tid);            
        }
    } 
}