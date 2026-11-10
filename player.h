typedef enum State{
    OFFLINE,
    ONLINE_IDLE,
    ONLINE_IN_GAME
}State;

typedef struct Player{
    char username[256];
    int socket;
    char bio[512];
    State state;        
}Player;

typedef enum Invite_state{
    NONE,
    PENDING,
    ACCEPTED,
    REFUSED
}Invite_state;

typedef struct Invite{
    char sender_username[256];
    Invite_state invite_state;
}Invite;

typedef struct Chat_message{   
    char sender_username[256];
    char message[1024]; 
}Chat_message;