typedef enum State{
    OFFLINE,
    ONLINE_IDLE,
    ONLINE_IN_GAME
}State;

typedef struct Player{
    char username[256];
    char bio[512];
    State state;    
}Player;