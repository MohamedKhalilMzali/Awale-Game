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
#include "game.h"

static Player player;
static pthread_mutex_t clients_lock=PTHREAD_MUTEX_INITIALIZER;
static char prompt[256]="Awale (not registered)";
static char buffer_in[1024];
static Invite invite;
static FriendRequest friendRequest;
static int pending=0;
static int FriendRequest_pending=0;


static volatile int in_game = 0;      
static volatile int my_side = -1;     
static char opponent_name[256] = "";

static pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t registration_cond = PTHREAD_COND_INITIALIZER;
static char* receiver_username;

static volatile int observing = -1;  
static char obs_name1[256] = "";
static char obs_name2[256] = "";


void display_banner(){
    printf("\n------------------------------------------------\n");
    printf("\n-----------------AWALE GAME---------------------\n");
    printf("\n------------------------------------------------\n");
}

void display_prompt(){
    pthread_mutex_lock(&clients_lock);
    printf("%s> ",prompt);
    fflush(stdout);
    pthread_mutex_unlock(&clients_lock);
}

void display_menu(){    
    printf("--------------------------------------------\n");
    printf("/add-bio [bio] \n");
    printf("/list-bio [username] \n");
    printf("/invite [username] \n");
    printf("/send-msg [username] [message] \n");
    printf("/list-online-users \n");
    printf("/list-online-games \n");
    printf("/add-friend [username] \n");
    printf("/list-friends \n");
    printf("/observer-mode [username]\n");
    printf("/stop-observe\n");
    printf("/quit \n");
    printf("--------------------------------------------\n");
}

void display_game_menu(){
    printf("\n========== In-GAME MENU ==========\n");
    printf("1-6 : Choose a position\n");
    printf("s : Save the game\n");
    printf("l : Load a game\n");
    printf("/send-msg [username] [message]\n");
    printf("/private-mode \n");
    printf("/help-menu \n");
    printf("q : Quit the current game\n");
    printf("====================================\n");
}

void close_comm(int connect_client_fd){
    close(connect_client_fd);
    printf("\n--------------------------------------\n");
    printf("[*] You left the server \n");
    printf("\n--------------------------------------\n");
    exit(EXIT_SUCCESS);  
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
    cJSON *games=cJSON_GetObjectItemCaseSensitive(item,"Games");
    if(cJSON_IsArray(games)){
        cJSON *game;
        printf("\n----------Ongoing Games------------\n");
        cJSON_ArrayForEach(game,games){
            cJSON* game_player1=cJSON_GetObjectItemCaseSensitive(game,"player 1");
            cJSON* game_player2=cJSON_GetObjectItemCaseSensitive(game,"player 2");
            cJSON* score_player_1=cJSON_GetObjectItemCaseSensitive(game,"score player 1");
            cJSON* score_player_2=cJSON_GetObjectItemCaseSensitive(game,"score player 2");
            
            printf("\n---------------------------------------------------------------\n");
            printf("Player 1: %s \n",game_player1->valuestring);
            printf("Player 2:  %s \n",game_player2->valuestring);
            printf("score player 1:  %d \n",score_player_1->valueint);
            printf("score player 2:  %d \n",score_player_2->valueint);
            printf("\n---------------------------------------------------------------\n");
        }
    }
}

void display_friends_from_json(cJSON *item){
    cJSON *friends=cJSON_GetObjectItemCaseSensitive(item,"Friends");
    if(cJSON_IsArray(friends)){
        cJSON *friend;
        printf("\n----------Friends------------\n");
        cJSON_ArrayForEach(friend,friends){
            if(cJSON_IsString(friend)){
                printf("%s \n", friend->valuestring);
            }            
        }
    }
}
void display_bio_from_json(cJSON *item){
    cJSON *bio=cJSON_GetObjectItemCaseSensitive(item,"Bio");
    if(cJSON_IsString(bio)){
        printf("[Bio]: %s\n", bio->valuestring);
    }
}

void display_awale_board(int pits[], int score0, int score1, int current)
{
    printf("\n========================================\n");
    printf("               AWALE GAME\n");
    printf("========================================\n");

    printf("\n Joueur 2 (haut) - Score : %d\n", score1);

    printf("       6     5     4     3     2     1\n");
    printf("    +-----+-----+-----+-----+-----+-----+\n");
    printf("    |");

    for (int i = 11; i >= 6; i--)
        printf(" %2d  |", pits[i]);

    printf("\n    +-----+-----+-----+-----+-----+-----+\n");
    printf("    |");

    for (int i = 0; i < 6; i++)
        printf(" %2d  |", pits[i]);

    printf("\n    +-----+-----+-----+-----+-----+-----+\n");
    printf("       1     2     3     4     5     6\n");

    printf(" Joueur 1 (bas) - Score : %d\n", score0);

    printf("\n Au tour du joueur %d.\n", current + 1);
    printf("========================================\n");
}


void *receive_msg(void *arg){
    int connect_client_fd=*(int *)arg;
    char *msg_content;
    char username[256];
    uint32_t msg_type,msg_length;

    while(1){
        if(recv(connect_client_fd,&msg_type,sizeof(msg_type),0)<=0){
            break;
        }
        msg_type=ntohl(msg_type);
        
                        
        if(recv(connect_client_fd,&msg_length,sizeof(msg_length),0)<=0){
            break;
        }        
        msg_length=ntohl(msg_length);
        

        msg_content=malloc(msg_length+1);
        if(recv(connect_client_fd,msg_content,msg_length,0)<=0){
            free(msg_content);
            break;            
        }
        msg_content[msg_length]='\0';                                

        switch(msg_type){
            case MSG_TEXT:{

                if(strcmp(msg_content,"USERNAME_AVAILABLE")==0){
                    pthread_mutex_lock(&clients_lock);
                    printf("Buffer_in in the thread: %s \n",buffer_in);
                    char *command=strtok(buffer_in," ");
                    char *username=strtok(NULL, " ");                   
                    if(username!=NULL){
                        snprintf(player.username,sizeof(player.username),"%s",username);                                       
                        snprintf(prompt,sizeof(prompt),"%s",player.username);
                    }                    
                    pthread_mutex_unlock(&clients_lock);
                }
                else if(strcmp(msg_content,"USERNAME_NOT_AVAILABLE")==0){
                    printf("[-] The requested username is already taken, please try again \n");
                }
                else if(strcmp(msg_content,"BIO_ADDED")==0){
                    printf("[+] Bio added successfully \n");
                }
                else if(strcmp(msg_content,"SERVER_DISCONNECTED")==0){
                    printf("[-] Server disconnected from the network \n");
                    close_comm(connect_client_fd);
                }
                else if(strcmp(msg_content,"INVALID_COMMAND")==0){
                    printf("[-] Server returned: invalid command \n");
                }
                else if(strcmp(msg_content,"PRIVATE_MODE_REFUSE")==0){
                    printf("[-] GAME IN PRIVATE MODE. The game is restricted to the player's friends only \n");
                }
                else if(strcmp(msg_content,"NO_FRIENDS")==0){
                    printf("[-] No friends yet \n");
                }
                
              
                else if (strncmp(msg_content, "GAME_BEGIN ", 11) == 0) {

                    int side = 0;
                    char opp[256] = "";

                    if (sscanf(msg_content + 11, "%d %255s", &side, opp) >= 1) {
                        in_game = 1;
                        my_side = side;
                        snprintf(opponent_name, sizeof(opponent_name), "%s", opp);

                        printf("\n*****************************************\n");
                        printf("  THE GAME WILL BEGIN AGAINST %s !\n", opponent_name);
                        printf("  You are player %d (%s).\n",
                            side + 1, side == 0 ? "bas" : "haut");
                        printf("*****************************************\n");
                        display_game_menu();
                    }
                }

                else if (strncmp(msg_content, "GAME_STATE ", 11) == 0) {

                    int current, score0, score1;
                    int pits[NB_PITS];
                    int offset = 0;
                    int valid = 1;

                    char *data = msg_content + 11;

                    if (sscanf(data, "%d %d %d%n",
                            &current, &score0, &score1, &offset) == 3) {

                        data += offset;

                        for (int i = 0; i < NB_PITS; i++) {
                            int consumed = 0;

                            if (sscanf(data, "%d%n", &pits[i], &consumed) != 1) {
                                valid = 0;
                                break;
                            }

                            data += consumed;
                        }

                        if (valid) {
                            display_awale_board(pits, score0, score1, current);

                            if (current == my_side)
                                printf("\n>>> C'est a VOUS de jouer (1 a 6, s, l, q) <<<\n");
                            else
                                printf("\n... En attente du coup de %s ...\n",
                                    opponent_name);
                        }
                        else {
                            printf("\n[-] Invalid Awale board received.\n");
                        }
                    }
                    else {
                        printf("\n[-] Invalid GAME_STATE message.\n");
                    }
                }

                else if (strncmp(msg_content, "GAME_OVER ", 10) == 0) {

                    int score0 = 0, score1 = 0, winner = 2, offset = 0;
                    const char *reason = "";

                    if (sscanf(msg_content + 10, "%d %d %d%n",
                            &score0, &score1, &winner, &offset) == 3) {
                        reason = msg_content + 10 + offset;
                        while (*reason == ' ') reason++;
                    }

                    printf("\n=========================================\n");
                    printf("          LA PARTIE EST TERMINEE\n");
                    if (*reason != '\0')
                        printf("  (%s)\n", reason);
                    printf("\n  Scores finaux :\n");
                    printf("  Joueur 1 : %d points\n", score0);
                    printf("  Joueur 2 : %d points\n", score1);

                    if (winner == 2)
                        printf("\n  Egalite !\n");
                    else if (winner == my_side)
                        printf("\n  >>> VOUS AVEZ GAGNE ! <<<\n");
                    else
                        printf("\n  Vous avez perdu. Le joueur %d est le vainqueur.\n",
                            winner + 1);
                    printf("=========================================\n");
                    printf("Vous etes de retour dans le lobby (/help).\n");

                    in_game = 0;
                    my_side = -1;
                }

                else if (strcmp(msg_content, "INVALID_MOVE") == 0) {
                    printf("\nCoup invalide ! Reessayez.\n");
                }
                else if (strcmp(msg_content, "NOT_YOUR_TURN") == 0) {
                    printf("\nCe n'est pas votre tour.\n");
                }
                else if (strcmp(msg_content, "UNKNOWN_COMMAND") == 0) {
                    printf("\nCommande invalide ! (tapez /help)\n");
                }
                else if (strcmp(msg_content, "NO_ACTIVE_GAME") == 0) {
                    printf("\nVous n'etes dans aucune partie.\n");
                }
                else if (strcmp(msg_content, "SAVE_OK") == 0) {
                    printf("\nPartie sauvegardee !\n");
                }
                else if (strcmp(msg_content, "SAVE_FAILED") == 0) {
                    printf("\nErreur de sauvegarde.\n");
                }
                else if (strcmp(msg_content, "LOAD_OK") == 0) {
                    printf("\nPartie chargee !\n");
                }
                else if (strcmp(msg_content, "LOAD_FAILED") == 0) {
                    printf("\nErreur de chargement (aucune sauvegarde valide).\n");
                }
                
                
                else if (strncmp(msg_content, "OBSERVE_OK ", 11) == 0) {
                    int id = -1;
                    char n1[256] = "", n2[256] = "";
                    if (sscanf(msg_content + 11, "%d %255s %255s", &id, n1, n2) == 3) {
                        observing = id;
                        snprintf(obs_name1, sizeof(obs_name1), "%s", n1);
                        snprintf(obs_name2, sizeof(obs_name2), "%s", n2);
                        printf("\n[+] Vous observez la partie %d : %s vs %s (/stop-observe pour arreter)\n",
                            id, n1, n2);
                    }
                }

                else if (strncmp(msg_content, "OBSERVE_STATE ", 14) == 0) {
                    int id, current, score0, score1, pits[NB_PITS];
                    int offset = 0, valid = 1;
                    char *data = msg_content + 14;

                    if (sscanf(data, "%d %d %d %d%n", &id, &current, &score0, &score1, &offset) == 4) {
                        data += offset;
                        for (int i = 0; i < NB_PITS; i++) {
                            int consumed = 0;
                            if (sscanf(data, "%d%n", &pits[i], &consumed) != 1) { valid = 0; break; }
                            data += consumed;
                        }
                        if (valid) {
                            printf("\n[OBSERVATION partie %d] Joueur 1 = %s, Joueur 2 = %s\n",
                                id, obs_name1, obs_name2);
                            display_awale_board(pits, score0, score1, current);
                            printf("... Au tour de %s ...\n", current == 0 ? obs_name1 : obs_name2);
                        }
                    }
                }

                else if (strncmp(msg_content, "OBSERVE_END ", 12) == 0) {
                    int id = 0, score0 = 0, score1 = 0, winner = 2, offset = 0;
                    const char *reason = "";
                    if (sscanf(msg_content + 12, "%d %d %d %d%n",
                            &id, &score0, &score1, &winner, &offset) == 4) {
                        reason = msg_content + 12 + offset;
                        while (*reason == ' ') reason++;
                    }
                    printf("\n=========================================\n");
                    printf("  PARTIE OBSERVEE %d TERMINEE\n", id);
                    if (*reason != '\0')
                        printf("  (%s)\n", reason);
                    printf("  %s : %d points\n", obs_name1, score0);
                    printf("  %s : %d points\n", obs_name2, score1);
                    if (winner == 2)
                        printf("  Egalite !\n");
                    else
                        printf("  Vainqueur : %s\n", winner == 0 ? obs_name1 : obs_name2);
                    printf("=========================================\n");
                    observing = -1;
                }
                else if (strcmp(msg_content, "OBSERVE_FAILED_UNKNOWN") == 0) {
                    printf("\n[-] Ce joueur n'existe pas.\n");
                }
                else if (strcmp(msg_content, "OBSERVE_FAILED_NO_GAME") == 0) {
                    printf("\n[-] Ce joueur n'est pas en train de jouer une partie.\n");
                }
                else if (strcmp(msg_content, "OBSERVE_FAILED_SELF") == 0) {
                    printf("\n[-] Vous ne pouvez pas vous observer vous-meme.\n");
                }
                else if (strcmp(msg_content, "OBSERVE_FAILED_BUSY") == 0) {
                    printf("\n[-] Impossible d'observer pendant que vous jouez une partie.\n");
                }
                else if (strcmp(msg_content, "OBSERVE_FAILED") == 0) {
                    printf("\n[-] Impossible d'observer cette partie.\n");
                }
                else if (strcmp(msg_content, "OBSERVE_STOPPED") == 0) {
                    observing = -1;
                    printf("\n[+] Vous n'observez plus aucune partie.\n");
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
                else if(strcmp(item->string,"Games")==0){                    
                    display_games_from_json(json);
                }
                else if(strcmp(item->string,"Friends")==0){
                    display_friends_from_json(json);
                }    
                else if(strcmp(item->string,"Bio")==0){
                    display_bio_from_json(json);
                }
            
            break;    
            }
            case MSG_INVITE:
            {   
                pthread_mutex_lock(&clients_lock);      
                
                memcpy(&invite,msg_content,sizeof(invite));
                invite.sender_username[sizeof(invite.sender_username)-1]='\0';
                
                printf("Sender: %s \n",invite.sender_username);
                printf("State: %d \n",invite.invite_state);

                if(invite.invite_state==PENDING){                                       
                    pending=1;
                    printf("[INVITE_NOTIFICATION] Invitation from %s to join a game: /accept, /refuse\n",invite.sender_username);                              
                }    
                else if(invite.invite_state==ACCEPTED){
                    printf("[INVITE_NOTIFICATION] Player %s accepted your invite \n",invite.sender_username);
                    printf("[+] STARTING THE GAME ... \n");
                }
                else if(invite.invite_state==REFUSED){
                    printf("[INVITE_NOTIFICATION] Player %s refused your invite \n",invite.sender_username);
                }                

                pthread_mutex_unlock(&clients_lock);

            break;
            }
            case MSG_CHAT:{
                Chat_message chat_msg;
                memcpy(&chat_msg,msg_content,sizeof(Chat_message));
                printf("\n[+] NEW MESSAGE \n");
                printf("%s: %s\n",chat_msg.sender_username,chat_msg.message);
            break;
            }
            case MSG_FRIEND_REQUEST:{
                pthread_mutex_lock(&clients_lock);      
                
                memcpy(&friendRequest,msg_content,sizeof(friendRequest));
                friendRequest.sender_username[sizeof(friendRequest.sender_username)-1]='\0';
                
                printf("Sender: %s \n",friendRequest.sender_username);

                if(friendRequest.req_state==REQUEST_PENDING){                                       
                    FriendRequest_pending=1;
                    printf("[REQUEST_NOTIFICATION] Friend request from %s : /accept-request, /refuse-request\n",friendRequest.sender_username);                              
                }    
                else if(friendRequest.req_state==REQUEST_ACCEPTED){
                    printf("[REQUEST_NOTIFICATION] Player %s accepted your request \n",friendRequest.receiver_username);                    
                }
                else if(friendRequest.req_state==REQUEST_REFUSED){
                    printf("[REQUEST_NOTIFICATION] Player %s refused your request \n",friendRequest.receiver_username);
                }                

                pthread_mutex_unlock(&clients_lock);                

            break;
            }
            
        }
        free(msg_content);
        display_prompt();                                            
    }
    close_comm(connect_client_fd);
    printf("[*] Server closed the communication \n");
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

    if(player.username!=NULL && player.username[0]!= '\0'){
        snprintf(prompt,sizeof(prompt),"%s",player.username);        
    }

    display_prompt();

    do{             
        
        fgets(buffer_in,sizeof(buffer_in),stdin);
        buffer_in[sizeof(buffer_in)-1]='\0';
        buffer_in[strcspn(buffer_in,"\n")]='\0';        

        if(buffer_in[0]=='\0'){
            display_prompt();
            continue;
        }

        if(pending==1){
            char *response=NULL;
            if(strcmp(buffer_in,"/accept")==0){
                printf("ACCEPTED \n");
                response="ACCEPTED";
            }
            else if(strcmp(buffer_in,"/refuse")==0){
                printf("REFUSED \n");
                response="REFUSED";
            }            
            
            pthread_mutex_lock(&clients_lock);
            pending=0;
            pthread_mutex_unlock(&clients_lock);

            if(send(connect_client_fd,response,strlen(response),0)<=0){
                printf("[-] Could not send data to the server \n");
                exit(EXIT_FAILURE);                
            }
            printf("Sent answer to the server \n ");
            continue;               
        }

        if(FriendRequest_pending==1){
            char *response=NULL;
            if(strcmp(buffer_in,"/accept-request")==0){
                printf("REQUEST_ACCEPTED \n");
                response="REQUEST_ACCEPTED";
            }
            else if(strcmp(buffer_in,"/refuse-request")==0){
                printf("REQUEST_REFUSED \n");
                response="REQUEST_REFUSED";
            }            
            
            pthread_mutex_lock(&clients_lock);
            FriendRequest_pending=0;
            pthread_mutex_unlock(&clients_lock);

            if(send(connect_client_fd,response,strlen(response),0)<=0){
                printf("[-] Could not send data to the server \n");
                exit(EXIT_FAILURE);                
            }
            printf("Sent answer to the server \n ");
            continue;               
        }

        else if(strcmp(buffer_in,"/help")==0){
            if(player.username[0]=='\0'){         
                printf("--------------------------------------------\n");           
                printf("/register [username] \n");
                printf("--------------------------------------------\n");
            }
            else{
                display_menu();
            }
            display_prompt();
            continue;
        }
        else if(strcmp(buffer_in,"/help-menu")==0){
            display_game_menu();
            display_prompt();
            continue;
        }

        else{         
            if(send(connect_client_fd,&buffer_in,strlen(buffer_in),0)<=0){
                printf("[-] Could not send data to the server \n");
                exit(EXIT_FAILURE);                
            }
            printf("Sent Buffer: %s \n",buffer_in);
        }                
    }while(strcmp(buffer_in,"/quit")!=0); 

    close_comm(connect_client_fd);
}