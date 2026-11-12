#define MAX_PLAYERS 100
#define MAX_GAMES 50
#define MAX_OBSERVERS 10

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
static int game_count=0;
static pthread_mutex_t players_lock=PTHREAD_MUTEX_INITIALIZER; 
static Game games[MAX_GAMES];
static int observers[MAX_GAMES][MAX_OBSERVERS];

static char invite_from[MAX_PLAYERS][256];
static char friend_request_from[MAX_PLAYERS][256];


static void send_observe_state(int game_id, int target)
{
    Awale *awale = &games[game_id].awale;
    char message[1024];

    int offset = snprintf(message, sizeof(message), "OBSERVE_STATE %d %d %d %d",
                          game_id, awale->current, awale->score[0], awale->score[1]);
    for (int i = 0; i < NB_PITS && offset < (int)sizeof(message); i++)
        offset += snprintf(message + offset, sizeof(message) - offset, " %d", awale->pits[i]);

    if (target != -1) {
        send_to_client(target, MSG_TEXT, message,sizeof(message));
        return;
    }
    for (int k = 0; k < MAX_OBSERVERS; k++)
        if (observers[game_id][k] > 0)
            send_to_client(observers[game_id][k], MSG_TEXT, message,sizeof(message));
}

static void stop_observing(int socket_fd)
{
    for (int g = 0; g < MAX_GAMES; g++)
        for (int k = 0; k < MAX_OBSERVERS; k++)
            if (observers[g][k] == socket_fd)
                observers[g][k] = 0;
}

static int start_observing(int socket_fd, int game_id)
{
    if (game_id < 0 || game_id >= MAX_GAMES || !games[game_id].active)
        return 0;

        if (games[game_id].player1_socket == socket_fd ||
        games[game_id].player2_socket == socket_fd)
        return 0;

    stop_observing(socket_fd);   
    for (int k = 0; k < MAX_OBSERVERS; k++) {
        if (observers[game_id][k] == 0) {
            observers[game_id][k] = socket_fd;
            return 1;
        }
    }
    return 0;                 
}

static void end_observation(int game_id, int winner, const char *reason)
{
    char message[512];
    snprintf(message, sizeof(message), "OBSERVE_END %d %d %d %d %s",
             game_id, games[game_id].awale.score[0], games[game_id].awale.score[1],
             winner, reason != NULL ? reason : "");
    for (int k = 0; k < MAX_OBSERVERS; k++) {
        if (observers[game_id][k] > 0)
            send_to_client(observers[game_id][k], MSG_TEXT, message,sizeof(message));
        observers[game_id][k] = 0;
    }
}

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
            games[i].state=PUBLIC_MODE;
            game_count++;

            return i;
        }
    }

    return -1;
}


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

    send_observe_state(game_id, -1);   /* observateurs */
}

static void send_game_begin(int game_id)
{
    Game *game = &games[game_id];
    char message[512];

    snprintf(message, sizeof(message), "GAME_BEGIN 0 %s", game->name2);
    send_to_client(game->player1_socket, MSG_TEXT, message, sizeof(message));

    snprintf(message, sizeof(message), "GAME_BEGIN 1 %s", game->name1);
    send_to_client(game->player2_socket, MSG_TEXT, message, sizeof(message));
}


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

cJSON* write_players_to_json(cJSON* players_json, cJSON* players_array, char *username){    
    cJSON *player=cJSON_CreateObject();
    cJSON_AddStringToObject(player,"username",username);
    cJSON_AddItemToArray(players_array,player);
    printf("[JSON] Added the username to the array \n");
    
    return players_json;
}

cJSON* write_games_to_json(cJSON* games_json, cJSON* games_array, char *game_player1, char *game_player2, int score_player_1, int score_player_2){    
    cJSON *game=cJSON_CreateObject();
    cJSON_AddStringToObject(game,"player 1",game_player1);
    cJSON_AddStringToObject(game,"player 2",game_player2);
    cJSON_AddNumberToObject(game,"score player 1",score_player_1);
    cJSON_AddNumberToObject(game,"score player 2",score_player_2);
    cJSON_AddItemToArray(games_array,game);
    printf("[JSON] Added the game to the array \n");
    
    return games_json;
}
cJSON* write_friends_to_json(cJSON* friends_json, Player* player){     
    cJSON *friends_arr=cJSON_AddArrayToObject(friends_json,"Friends");
    for(int i=0;i<player->nb_friends;i++){        
        cJSON_AddItemToArray(friends_arr,cJSON_CreateString(player->friends[i]));
    }    
    return friends_json;
}

cJSON* write_bio_to_json(cJSON* bio_json, const char *bio){     
    cJSON_AddStringToObject(bio_json,"Bio",bio);      
    return bio_json;
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
            if(player_c[i].state==ONLINE_IDLE || player_c[i].state==ONLINE_IN_GAME){
                printf("Username: %s\n",player_c[i].username);                
                online_users=write_players_to_json(players_json,players_array,player_c[i].username);                
            }
        }                        
    }
    return players_json;
}

cJSON* display_online_games(){
    cJSON *online_games=NULL;
    cJSON *games_json=cJSON_CreateObject();
    cJSON *games_array=cJSON_AddArrayToObject(games_json,"Games");
    char *json=NULL;
    
    if(game_count>0){
        printf("Games count: %d \n",game_count);
        for(int i=0;i<game_count;i++){
            if(games[i].active==1){
                printf("Player 1: %s\n",games[i].name1);
                printf("Player 2: %s \n",games[i].name2);                
                online_games=write_games_to_json(games_json,games_array,games[i].name1,games[i].name2, games[i].awale.score[0], games[i].awale.score[1]);                
            }
            else{
                printf("[*] No active game \n");
            }
        }                        
    }
    return games_json;
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

static int registered_index_by_socket(int socket_fd)
{
    for (int i = 0; i < player_count; i++) {
        if (player_c[i].socket == socket_fd && player_c[i].state != OFFLINE)
            return i;
    }
    return -1;
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

void send_invite(int sender_socket, const char *receiver_username){
    char *sender_username=search_player_by_socket(sender_socket);
    Player* sender=search_player(sender_username);
    Player* receiver=search_player(receiver_username);

    if(sender==NULL){
        printf("[-] The sender is not registered \n");
        return;
    }
    if(receiver==NULL){
        printf("[-] User %s does not exist \n",receiver_username);
        send_to_client(sender_socket,MSG_TEXT,"PLAYER_NOT_FOUND",strlen("PLAYER_NOT_FOUND"));
        return;
    }
    if(receiver==sender){
        send_to_client(sender_socket,MSG_TEXT,"CANNOT_TARGET_SELF",strlen("CANNOT_TARGET_SELF"));
        return;
    }
    if(sender->state==ONLINE_IN_GAME){
        send_to_client(sender_socket,MSG_TEXT,"YOU_ARE_IN_GAME",strlen("YOU_ARE_IN_GAME"));
        return;
    }
    if(receiver->state==ONLINE_IN_GAME){
        printf("[-] %s is already in a game \n",receiver->username);
        send_to_client(sender_socket,MSG_TEXT,"PLAYER_IN_GAME",strlen("PLAYER_IN_GAME"));
        return;
    }
    if(receiver->state==OFFLINE){
        send_to_client(sender_socket,MSG_TEXT,"PLAYER_OFFLINE",strlen("PLAYER_OFFLINE"));
        return;
    }

    int r = receiver - player_c;
    if(invite_from[r][0]!='\0'){
        send_to_client(sender_socket,MSG_TEXT,"PLAYER_BUSY",strlen("PLAYER_BUSY"));
        return;
    }

    snprintf(invite_from[r],sizeof(invite_from[r]),"%s",sender->username);

    Invite invite;
    memset(&invite,0,sizeof(invite));
    snprintf(invite.sender_username,sizeof(invite.sender_username),"%s",sender->username);
    invite.invite_state=PENDING;

    send_to_client(receiver->socket,MSG_INVITE,&invite,sizeof(invite));
    send_to_client(sender_socket,MSG_TEXT,"INVITE_SENT",strlen("INVITE_SENT"));
    printf("[+] Sending invite from %s to %s on socket %d \n",sender->username,receiver->username,receiver->socket);
}

void answer_invite(int receiver_socket, int accepted){
    char *receiver_username=search_player_by_socket(receiver_socket);
    Player* receiver=search_player(receiver_username);
    if(receiver==NULL){
        return;
    }

    int r = receiver - player_c;
    if(invite_from[r][0]=='\0'){
        send_to_client(receiver_socket,MSG_TEXT,"NO_INVITE",strlen("NO_INVITE"));
        return;
    }

    Player* sender=search_player(invite_from[r]);
    invite_from[r][0]='\0';

    if(sender==NULL || sender->state!=ONLINE_IDLE || receiver->state!=ONLINE_IDLE){
        send_to_client(receiver_socket,MSG_TEXT,"INVITE_CANCELLED",strlen("INVITE_CANCELLED"));
        return;
    }

    Invite invite;
    memset(&invite,0,sizeof(invite));
    snprintf(invite.sender_username,sizeof(invite.sender_username),"%s",receiver->username);

    if(!accepted){
        invite.invite_state=REFUSED;
        send_to_client(sender->socket,MSG_INVITE,&invite,sizeof(invite));
        return;
    }

    invite.invite_state=ACCEPTED;
    send_to_client(sender->socket,MSG_INVITE,&invite,sizeof(invite));
    printf("[+] Running the game ... \n");

    int game_id = create_game(sender->socket, receiver->socket,
                              sender->username, receiver->username);
    if (game_id == -1) {
        return;
    }

    sender->state = ONLINE_IN_GAME;
    receiver->state = ONLINE_IN_GAME;

    send_game_begin(game_id);
    send_game_state(game_id);
}

void add_friend(int sender_socket, const char* request_receiver){
    char *request_sender=search_player_by_socket(sender_socket);
    Player* sender=search_player(request_sender);
    Player* receiver=search_player(request_receiver);

    if(sender==NULL){
        printf("[-] The sender is not registered \n");
        return;
    }
    if(receiver==NULL){
        printf("[-] User %s does not exist \n",request_receiver);
        send_to_client(sender_socket,MSG_TEXT,"PLAYER_NOT_FOUND",strlen("PLAYER_NOT_FOUND"));
        return;
    }
    if(receiver==sender){
        send_to_client(sender_socket,MSG_TEXT,"CANNOT_TARGET_SELF",strlen("CANNOT_TARGET_SELF"));
        return;
    }
    for(int i=0;i<sender->nb_friends;i++){
        if(strcmp(sender->friends[i],receiver->username)==0){
            send_to_client(sender_socket,MSG_TEXT,"ALREADY_FRIENDS",strlen("ALREADY_FRIENDS"));
            return;
        }
    }
    if(receiver->state==OFFLINE){
        send_to_client(sender_socket,MSG_TEXT,"PLAYER_OFFLINE",strlen("PLAYER_OFFLINE"));
        return;
    }

    int r = receiver - player_c;
    if(friend_request_from[r][0]!='\0'){
        send_to_client(sender_socket,MSG_TEXT,"PLAYER_BUSY",strlen("PLAYER_BUSY"));
        return;
    }

    snprintf(friend_request_from[r],sizeof(friend_request_from[r]),"%s",sender->username);

    FriendRequest friend_req;
    memset(&friend_req,0,sizeof(friend_req));
    snprintf(friend_req.sender_username,sizeof(friend_req.sender_username),"%s",sender->username);
    snprintf(friend_req.receiver_username,sizeof(friend_req.receiver_username),"%s",receiver->username);
    friend_req.req_state=REQUEST_PENDING;

    send_to_client(receiver->socket,MSG_FRIEND_REQUEST,&friend_req,sizeof(friend_req));
    send_to_client(sender_socket,MSG_TEXT,"FRIEND_REQUEST_SENT",strlen("FRIEND_REQUEST_SENT"));
    printf("[+] Sending a friend request from %s to %s on socket %d \n",sender->username,receiver->username,receiver->socket);
}

void answer_friend_request(int receiver_socket, int accepted){
    char *receiver_username=search_player_by_socket(receiver_socket);
    Player* receiver=search_player(receiver_username);
    if(receiver==NULL){
        return;
    }

    int r = receiver - player_c;
    if(friend_request_from[r][0]=='\0'){
        send_to_client(receiver_socket,MSG_TEXT,"NO_FRIEND_REQUEST",strlen("NO_FRIEND_REQUEST"));
        return;
    }

    Player* sender=search_player(friend_request_from[r]);
    friend_request_from[r][0]='\0';
    if(sender==NULL){
        return;
    }

    FriendRequest friend_req;
    memset(&friend_req,0,sizeof(friend_req));
    snprintf(friend_req.sender_username,sizeof(friend_req.sender_username),"%s",sender->username);
    snprintf(friend_req.receiver_username,sizeof(friend_req.receiver_username),"%s",receiver->username);

    if(!accepted){
        friend_req.req_state=REQUEST_REFUSED;
        send_to_client(sender->socket,MSG_FRIEND_REQUEST,&friend_req,sizeof(friend_req));
        return;
    }

    if(sender->nb_friends<MAX_FRIENDS && receiver->nb_friends<MAX_FRIENDS){
        snprintf(sender->friends[sender->nb_friends],sizeof(sender->friends[0]),"%s",receiver->username);
        snprintf(receiver->friends[receiver->nb_friends],sizeof(receiver->friends[0]),"%s",sender->username);
        sender->nb_friends++;
        receiver->nb_friends++;
    }
    friend_req.req_state=REQUEST_ACCEPTED;
    send_to_client(sender->socket,MSG_FRIEND_REQUEST,&friend_req,sizeof(friend_req));
}

cJSON* display_friends(const char* username){
    cJSON *friends_json=cJSON_CreateObject();
    cJSON *friends=NULL;

    Player *player=search_player(username);
    if(player==NULL){
        printf("Player %s does not exist \n", username);
        return NULL;
    }
    
    for(int i=0;i<player->nb_friends;i++){    
        friends=write_friends_to_json(friends_json,player);
    }
    return friends_json;
}

cJSON* display_bio(const char *username){
    Player *p=search_player(username);    
    cJSON* bio_json=cJSON_CreateObject();
    cJSON* bio_p=NULL; 

    if(p==NULL){
        printf("User %s does not exist \n", username);
        return NULL;
    }
    bio_p=write_bio_to_json(bio_json,p->bio);
    return bio_json; 
}

void restrict_game_to_friends(const char* username){
    Player *p=search_player(username);
    if(p==NULL){
        printf("Player %s does not exist \n", username);
        return;
    }
    for(int i=0;i<game_count;i++){
        if(games[i].player1_socket==p->socket || games[i].player2_socket==p->socket){
            games[i].state=PRIVATE_MODE;
            printf("[+] Changed the game state to private \n");
        }
    }
}
int observer_in_friends(Player *target, Player *observer){
    if(target==NULL){
        printf("Player %s does not exist \n",target->username);
        return -1;
    }
    if(observer==NULL){
        printf("Player %s does not exist \n",observer->username);
        return -1;
    }
    
    if(target->nb_friends==0){
        return -1;
    }

    for(int i=0;i<target->nb_friends;i++){
        if(strcmp(target->friends[i],observer->username)==0){
            printf("[+] Observer in the list of friends of the target player \n");
            return 1;
        }
    }
return -1;
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



         else if(strcmp(command,"/list-online-games")==0){
            pthread_mutex_lock(&players_lock);
            cJSON* onlineGames_json=display_online_games();         
            pthread_mutex_unlock(&players_lock);                         
            char* online_g=cJSON_PrintUnformatted(onlineGames_json);                                
            send_to_client(connect_socket_fd,MSG_JSON,online_g,strlen(online_g));                        
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
        else if(strcmp(command,"/list-bio")==0){
            char *username=strtok_r(NULL," ",&save_ptr);  

            pthread_mutex_lock(&players_lock);
            cJSON *bioJson=display_bio(username);
            pthread_mutex_unlock(&players_lock);
            
            char *bio_p=cJSON_PrintUnformatted(bioJson);

            send_to_client(connect_socket_fd,MSG_JSON,bio_p,strlen(bio_p));
        }

        else if(strcmp(command,"/invite")==0){
            char *receiver_username=strtok_r(NULL, " ",&save_ptr);

            pthread_mutex_lock(&players_lock);
            send_invite(connect_socket_fd,receiver_username);
            pthread_mutex_unlock(&players_lock); 
        }

        else if(strcmp(command,"ACCEPTED")==0 || strcmp(command,"REFUSED")==0){
            pthread_mutex_lock(&players_lock);
            answer_invite(connect_socket_fd, strcmp(command,"ACCEPTED")==0);
            pthread_mutex_unlock(&players_lock);
        }

        else if(strcmp(command,"/add-friend")==0){
            char *receiver_username=strtok_r(NULL, " ",&save_ptr);

            pthread_mutex_lock(&players_lock);
            add_friend(connect_socket_fd,receiver_username);
            pthread_mutex_unlock(&players_lock); 
        }

        else if(strcmp(command,"REQUEST_ACCEPTED")==0 || strcmp(command,"REQUEST_REFUSED")==0){
            pthread_mutex_lock(&players_lock);
            answer_friend_request(connect_socket_fd, strcmp(command,"REQUEST_ACCEPTED")==0);
            pthread_mutex_unlock(&players_lock);
        }

        else if(strcmp(command,"/list-friends")==0){
            pthread_mutex_lock(&players_lock);
            char *sender_username=search_player_by_socket(connect_socket_fd);
            
            Player *player=search_player(sender_username);
            if(player->nb_friends==0){
                pthread_mutex_unlock(&players_lock);
                char *response="NO_FRIENDS";
                send_to_client(connect_socket_fd,MSG_TEXT,response,strlen(response));  
                continue;
            }

            cJSON* friends_json=display_friends(sender_username);
            pthread_mutex_unlock(&players_lock);
            
            char *friends_p=cJSON_PrintUnformatted(friends_json);
            send_to_client(connect_socket_fd,MSG_JSON,friends_p,strlen(friends_p));                                  
             
        }

        else if(strcmp(command,"/send-msg")==0){
            char *sender_username=search_player_by_socket(connect_socket_fd);
            char *receiver_username=strtok_r(NULL, " ",&save_ptr);
            char *msg=strtok_r(NULL,"\r\n",&save_ptr);

            pthread_mutex_lock(&players_lock);
            send_message_chat(sender_username,receiver_username,msg);     
            pthread_mutex_unlock(&players_lock);           
        }

        else if(strcmp(command,"/private-mode")==0){
            pthread_mutex_lock(&players_lock);
            char *sender_username=search_player_by_socket(connect_socket_fd);
            restrict_game_to_friends(sender_username);
            pthread_mutex_unlock(&players_lock);
        }

        else if(strcmp(command,"/quit")==0){
            char *username=search_player_by_socket(connect_socket_fd);                
            modify_user_state(username,OFFLINE);
            leave_game_locked(connect_socket_fd,
                      "The opponent session disconnected");
            close(connect_socket_fd);     
        }

        else if (strcmp(command, "/observer-mode") == 0) {

            char *target_name = strtok_r(NULL, " \r\n", &save_ptr);
            
            if (target_name == NULL) {
                send_to_client(connect_socket_fd, MSG_TEXT,
                               "Usage: /observer-mode [username]", sizeof("Usage: /observer-mode [username]"));

                continue;
            }

            char message[600];
            const char *error = NULL;
            int game_id = -1;
            int game_index = -1;

            pthread_mutex_lock(&players_lock);
            int idx = registered_index_by_socket(connect_socket_fd);
            Player *target = search_player(target_name);
            
            char *observer_username=search_player_by_socket(connect_socket_fd);
            Player *observer=search_player(observer_username);

            printf("Found the observer username from the observer object: %s \n",observer->username);

            for(int i=0;i<game_count;i++){
                if(games[i].player1_socket==target->socket || games[i].player2_socket==target->socket){
                    game_index=i;
                    printf("Found the game of that target player: %d \n",game_index);
                    break;
                }
            }
            
            int allowed=1;

            if(games[game_index].state==PRIVATE_MODE){

                printf("Game in PRIVATE MODE \n");

                if(observer_in_friends(target,observer)==-1){
                    char *response="PRIVATE_MODE_REFUSE";
                    send_to_client(connect_socket_fd,MSG_TEXT,response,strlen(response));
                    allowed=0;
                }
                else if(observer_in_friends(target,observer)==1){
                    printf("[+] Observer in the target player's group of friends \n");
                    allowed=1;
                }
            }
            printf("Allowed: %d \n",allowed);

            if(allowed==0){
                pthread_mutex_unlock(&players_lock);
                continue;
            }           
            if(allowed==1){
                if (idx == -1)
                error = "OBSERVE_FAILED";
                else if (player_c[idx].state != ONLINE_IDLE)
                    error = "OBSERVE_FAILED_BUSY";       
                else if (target == NULL)
                    error = "OBSERVE_FAILED_UNKNOWN";       
                else if (target == &player_c[idx])
                    error = "OBSERVE_FAILED_SELF";
                else if (target->state != ONLINE_IN_GAME ||
                            (game_id = find_game_by_socket(target->socket, NULL)) == -1)
                    error = "OBSERVE_FAILED_NO_GAME";      
                else if (!start_observing(connect_socket_fd, game_id))
                    error = "OBSERVE_FAILED";

                if (error == NULL) {
                    snprintf(message, sizeof(message), "OBSERVE_OK %d %s %s",
                                game_id, games[game_id].name1, games[game_id].name2);
                    send_to_client(connect_socket_fd, MSG_TEXT, message, sizeof(message));
                    send_observe_state(game_id, connect_socket_fd);
                }
                pthread_mutex_unlock(&players_lock);

                if (error != NULL)
                    send_to_client(connect_socket_fd, MSG_TEXT, error, strlen(error));
            }
        }
            

        else if (strcmp(command, "/stop-observe") == 0) {
            pthread_mutex_lock(&players_lock);
            stop_observing(connect_socket_fd);
            pthread_mutex_unlock(&players_lock);
            send_to_client(connect_socket_fd, MSG_TEXT, "OBSERVE_STOPPED", sizeof("OBSERVE_STOPPED"));
        }

        
        else if (strcmp(command, "s") == 0) {
            handle_game_save(connect_socket_fd);
        }
        else if (strcmp(command, "l") == 0) {
            handle_game_load(connect_socket_fd);
        }
        else if (strcmp(command, "q") == 0) {
            handle_game_quit(connect_socket_fd);
        }

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
    
    pthread_mutex_lock(&players_lock);

    leave_game_locked(connect_socket_fd,"Opponent disconnected");
    stop_observing(connect_socket_fd);

    pthread_mutex_unlock(&players_lock);

    
    printf("[*] Closing communication with client.\n");
    close(connect_socket_fd);

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
