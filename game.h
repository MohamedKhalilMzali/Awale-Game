#define NB_PITS 12
#define PITS_PER_PLAYER 6
#define INITIAL_SEEDS 4

typedef struct {
    int pits[NB_PITS];
    int score[2];
    int current;
} Awale;

typedef struct {
    Awale awale;
    int player1_socket;
    int player2_socket;
    char name1[256];
    char name2[256];
    int active;
} Game;


/* Initialisation et affichage */
void awale_init(Awale *g);
void awale_print(const Awale *g);

/* Gestion des coups */
int awale_play_move(Awale *g, int pit_number);
int awale_has_legal_move(const Awale *g);
int awale_is_game_over(const Awale *g);
void awale_collect_remaining(Awale *g);

/* Sauvegarde et chargement */
int awale_save(const Awale *g, const char *filename);
int awale_load(Awale *g, const char *filename);