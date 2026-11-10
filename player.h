#define MAX_FRIENDS 50

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
    char friends[MAX_FRIENDS][256];
    int nb_friends;        
}Player;

typedef enum Invite_state{
    NONE,
    PENDING,
    ACCEPTED,
    REFUSED
}Invite_state;

typedef enum FriendRequest_state{
    REQUEST_NONE,
    REQUEST_PENDING,
    REQUEST_ACCEPTED,
    REQUEST_REFUSED
}FriendRequest_state;

typedef struct Invite{
    char sender_username[256];
    Invite_state invite_state;
}Invite;

typedef struct FriendRequest{
    char sender_username[256];
    FriendRequest_state req_state;
}FriendRequest;


typedef struct Chat_message{   
    char sender_username[256];
    char message[1024]; 
}Chat_message;