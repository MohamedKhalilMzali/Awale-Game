#define MAX_PLAYERS 100
#define MAX_GAMES 50

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <ctype.h>

#include "player.h"
#include "cjson/cJSON.h"
#include "network_server.h"
#include "game.h"

static Player player_c[MAX_PLAYERS];
static int player_count=0;
static pthread_mutex_t players_lock=PTHREAD_MUTEX_INITIALIZER; 
static Game games[MAX_GAMES];

static int create_game(int player1_socket, int player2_socket,
                       const char *name1, const char *name2) {
    for (int i = 0; i < MAX_GAMES; i++) {
        if (!games[i].active) {
            awale_init(&games[i].awale);

            games[i].player1_socket = player1_socket;
            games[i].player2_socket = player2_socket;
            snprintf(games[i].name1, sizeof(games[i].name1), "%s", name1);
            snprintf(games[i].name2, sizeof(games[i].name2), "%s", name2);
            games[i].active = 1;

            return i;
        }
    }

    return -1;
}


/* Retrouve la partie active d'un joueur (et son côté : 0 ou 1) */
static int find_game_by_socket(int client_socket, int *side)
{
    for (int i = 0; i < MAX_GAMES; i++) {
        if (!games[i].active)
            continue;
        if (games[i].player1_socket == client_socket) {
            if (side) *side = 0;
            return i;
        }
        if (games[i].player2_socket == client_socket) {
            if (side) *side = 1;
            return i;
        }
    }
    return -1;
}

static void set_state_by_socket(int socket_fd, State state)
{
    for (int i = 0; i < player_count; i++) {
        if (player_c[i].socket == socket_fd)
            player_c[i].state = state;
    }
}

/* Envoie le plateau aux deux joueurs */
static void send_game_state(int game_id)
{
    Game *game = &games[game_id];
    Awale *awale = &game->awale;

    char message[1024];

    int offset = snprintf(message, sizeof(message),
                          "GAME_STATE %d %d %d",
                          awale->current,
                          awale->score[0],
                          awale->score[1]);

    for (int i = 0; i < NB_PITS && offset < (int)sizeof(message); i++) {
        offset += snprintf(message + offset, sizeof(message) - offset,
                           " %d", awale->pits[i]);
    }

    send_to_client(game->player1_socket, MSG_TEXT, message, sizeof(message));
    send_to_client(game->player2_socket, MSG_TEXT, message, sizeof(message));
}

/* Annonce le début de la partie : qui est joueur 1 / joueur 2 */
static void send_game_begin(int game_id)
{
    Game *game = &games[game_id];
    char message[512];

    snprintf(message, sizeof(message), "GAME_BEGIN 0 %s", game->name2);
    send_to_client(game->player1_socket, MSG_TEXT, message, sizeof(message));

    snprintf(message, sizeof(message), "GAME_BEGIN 1 %s", game->name1);
    send_to_client(game->player2_socket, MSG_TEXT, message, sizeof(message));
}

/* Termine la partie : envoie GAME_OVER <score0> <score1> <vainqueur>
 * vainqueur : 0 = joueur 1, 1 = joueur 2, 2 = égalité
 * puis remet les deux joueurs en ONLINE_IDLE.
 * reason : texte affiché aux joueurs (peut être NULL). */
static void finish_game(int game_id, int winner, const char *reason)
{
    Game *game = &games[game_id];
    char result[512];

    snprintf(result, sizeof(result), "GAME_OVER %d %d %d %s",
             game->awale.score[0], game->awale.score[1], winner,
             reason != NULL ? reason : "");

    send_to_client(game->player1_socket, MSG_TEXT, result, sizeof(result));
    send_to_client(game->player2_socket, MSG_TEXT, result, sizeof(result));

    set_state_by_socket(game->player1_socket, ONLINE_IDLE);
    set_state_by_socket(game->player2_socket, ONLINE_IDLE);

    game->active = 0;
}

/* Nom du fichier de sauvegarde d'une partie (caractères sûrs uniquement) */
static void save_filename(const Game *game, char *out, size_t size)
{
    char a[64], b[64];
    size_t n;

    n = 0;
    for (const char *p = game->name1; *p && n < sizeof(a) - 1; p++)
        a[n++] = (isalnum((unsigned char)*p) || *p == '_' || *p == '-') ? *p : '_';
    a[n] = '\0';

    n = 0;
    for (const char *p = game->name2; *p && n < sizeof(b) - 1; p++)
        b[n++] = (isalnum((unsigned char)*p) || *p == '_' || *p == '-') ? *p : '_';
    b[n] = '\0';

    snprintf(out, size, "awale_save_%s_vs_%s.txt", a, b);
}

/* ------------------------------------------------------------------ */
/*  Commandes de jeu (équivalent du menu de part0/main.c)              */
/* ------------------------------------------------------------------ */

/* Coups 1 à 6 */
static void handle_game_move(int client_socket, int pit_number)
{
    pthread_mutex_lock(&players_lock);

    int player_side = -1;
    int game_id = find_game_by_socket(client_socket, &player_side);

    if (game_id == -1) {
        pthread_mutex_unlock(&players_lock);
        send_to_client(client_socket, MSG_TEXT, "NO_ACTIVE_GAME", sizeof("NO_ACTIVE_GAME"));
        return;
    }

    Game *game = &games[game_id];

    if (game->awale.current != player_side) {
        pthread_mutex_unlock(&players_lock);
        send_to_client(client_socket, MSG_TEXT, "NOT_YOUR_TURN",sizeof("NOT_YOUR_TURN"));
        return;
    }

    if (!awale_play_move(&game->awale, pit_number)) {
        pthread_mutex_unlock(&players_lock);
        send_to_client(client_socket, MSG_TEXT, "INVALID_MOVE",sizeof("INVALID_MOVE"));
        return;
    }

    if (awale_is_game_over(&game->awale)) {
        awale_collect_remaining(&game->awale);
        send_game_state(game_id);

        int winner;
        if (game->awale.score[0] > game->awale.score[1])      winner = 0;
        else if (game->awale.score[1] > game->awale.score[0]) winner = 1;
        else                                                  winner = 2;

        finish_game(game_id, winner, "Fin de la partie");
    }
    else {
        send_game_state(game_id);
    }

    pthread_mutex_unlock(&players_lock);
}

/* "s" : sauvegarder la partie en cours */
static void handle_game_save(int client_socket)
{
    pthread_mutex_lock(&players_lock);

    int game_id = find_game_by_socket(client_socket, NULL);
    if (game_id == -1) {
        pthread_mutex_unlock(&players_lock);
        send_to_client(client_socket, MSG_TEXT, "NO_ACTIVE_GAME",sizeof("NO_ACTIVE_GAME"));
        return;
    }

    char filename[512];
    save_filename(&games[game_id], filename, sizeof(filename));

    if (awale_save(&games[game_id].awale, filename)) {
        send_to_client(games[game_id].player1_socket, MSG_TEXT, "SAVE_OK",sizeof("SAVE_OK"));
        send_to_client(games[game_id].player2_socket, MSG_TEXT, "SAVE_OK",sizeof("SAVE_OK"));
    }
    else {
        send_to_client(client_socket, MSG_TEXT, "SAVE_FAILED",sizeof("SAVE_FAILED"));
    }

    pthread_mutex_unlock(&players_lock);
}

/* "l" : recharger la dernière sauvegarde de cette partie */
static void handle_game_load(int client_socket)
{
    pthread_mutex_lock(&players_lock);

    int game_id = find_game_by_socket(client_socket, NULL);
    if (game_id == -1) {
        pthread_mutex_unlock(&players_lock);
        send_to_client(client_socket, MSG_TEXT, "NO_ACTIVE_GAME",sizeof("NO_ACTIVE_GAME"));
        return;
    }

    char filename[512];
    save_filename(&games[game_id], filename, sizeof(filename));

    if (awale_load(&games[game_id].awale, filename)) {
        send_to_client(games[game_id].player1_socket, MSG_TEXT, "LOAD_OK",sizeof("LOAD_OK"));
        send_to_client(games[game_id].player2_socket, MSG_TEXT, "LOAD_OK",sizeof("LOAD_OK"));
        send_game_state(game_id);
    }
    else {
        send_to_client(client_socket, MSG_TEXT, "LOAD_FAILED",sizeof("LOAD_FAILED"));
    }

    pthread_mutex_unlock(&players_lock);
}

/* "q" : abandonner la partie (l'adversaire gagne), retour au lobby.
 * Appelée aussi quand un joueur se déconnecte en pleine partie.
 * Doit être appelée avec players_lock déjà verrouillé. */
static void leave_game_locked(int client_socket, const char *reason)
{
    int side = -1;
    int game_id = find_game_by_socket(client_socket, &side);

    if (game_id == -1)
        return;

    finish_game(game_id, 1 - side, reason);
}

static void handle_game_quit(int client_socket)
{
    pthread_mutex_lock(&players_lock);

    if (find_game_by_socket(client_socket, NULL) == -1) {
        pthread_mutex_unlock(&players_lock);
        send_to_client(client_socket, MSG_TEXT, "NO_ACTIVE_GAME",sizeof("NO_ACTIVE_GAME"));
        return;
    }

    leave_game_locked(client_socket, "Un joueur a abandonne la partie");

    pthread_mutex_unlock(&players_lock);
}


/*------------------------------------------------------------------------------------------------------------*/


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

void send_message_chat(const char *sender_username, const char *receiver_username, const char* msg){
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

    if(msg==NULL){
        return;
    }
  
    if (fcntl(receiver->socket, F_GETFD) == -1) {
        perror("Receiver socket is invalid");
    }

    Chat_message chat_msg;
    snprintf(chat_msg.sender_username,sizeof(chat_msg.sender_username),"%s",sender_username);
    snprintf(chat_msg.message,sizeof(chat_msg.message),"%s",msg);

    
    send_to_client(receiver->socket,MSG_CHAT,&chat_msg,sizeof(chat_msg));
    printf("[+] Sending message %s from %s to %s on socket %d \n",chat_msg.message,chat_msg.sender_username,receiver->username,receiver->socket);
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
    
    char buffer_out[256];
    recv_from_client(receiver->socket,buffer_out);
    if(strcmp(buffer_out,"REFUSED")==0){
        invite.invite_state=REFUSED;        
        send_to_client(sender->socket,MSG_INVITE,&invite,sizeof(invite));
    }
    if(strcmp(buffer_out,"ACCEPTED")==0){
        //running play_game function which launches the game between both players
        invite.invite_state=ACCEPTED;
        send_to_client(sender->socket,MSG_INVITE,&invite,sizeof(invite));
        printf("[+] Running the game ... \n");
        
        // Créer une nouvelle partie Awale
        int game_id = create_game(sender->socket, receiver->socket,
                                sender->username, receiver->username);                                

        if (game_id == -1) {
            return;
        }

        // Les deux joueurs passent en partie
        sender->state = ONLINE_IN_GAME;
        receiver->state = ONLINE_IN_GAME;

        send_game_begin(game_id);
        send_game_state(game_id);
    }
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

        if (n==0) {
            printf("Client disconnected.\n");
            break;
        }
        if (n<0) {
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
                      
            send_invite(sender_username,receiver_username);
            pthread_mutex_unlock(&players_lock); 
        }

        else if(strcmp(command,"/send-msg")==0){
            char *sender_username=search_player_by_socket(connect_socket_fd);
            char *receiver_username=strtok_r(NULL, " ",&save_ptr);
            char *msg=strtok_r(NULL,"\r\n",&save_ptr);

            pthread_mutex_lock(&players_lock);
            send_message_chat(sender_username,receiver_username,msg);     
            pthread_mutex_unlock(&players_lock);           
        }

        else if(strcmp(command,"/quit")==0){
            char *username=search_player_by_socket(connect_socket_fd);                
            modify_user_state(username,OFFLINE);
            leave_game_locked(connect_socket_fd,
                      "The opponent session disconnected");
            close(connect_socket_fd);     
        }
        
        
        // SAUVEGARDER / CHARGER / ABANDONNER (pendant une partie)
        else if (strcmp(command, "s") == 0) {
            handle_game_save(connect_socket_fd);
        }
        else if (strcmp(command, "l") == 0) {
            handle_game_load(connect_socket_fd);
        }
        else if (strcmp(command, "q") == 0) {
            handle_game_quit(connect_socket_fd);
        }

        // COUP AWALE
        else if (strlen(command) == 1 &&
                command[0] >= '1' && command[0] <= '6')
        {
            handle_game_move(connect_socket_fd, command[0] - '0');
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