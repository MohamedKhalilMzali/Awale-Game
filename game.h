#define NB_PITS 12
#define PITS_PER_PLAYER 6
#define INITIAL_SEEDS 4

typedef struct {
    int pits[NB_PITS];
    int score[2];
    int current;
} Awale;

typedef enum GameState{
    PRIVATE_MODE=0,
    PUBLIC_MODE=1
}GameState;

typedef struct {
    Awale awale;
    int player1_socket;
    int player2_socket;
    char name1[256];
    char name2[256];
    int active;
    GameState state;
} Game;


void awale_init(Awale *g);
void awale_print(const Awale *g);

int awale_play_move(Awale *g, int pit_number);
int awale_has_legal_move(const Awale *g);
int awale_is_game_over(const Awale *g);
void awale_collect_remaining(Awale *g);

int awale_save(const Awale *g, const char *filename);
int awale_load(Awale *g, const char *filename);