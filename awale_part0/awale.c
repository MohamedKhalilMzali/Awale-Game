#include <stdio.h>
#include "awale.h"

static int count_seeds(const Awale *g, int player)
{
    int total = 0; 
    int start = player * PITS_PER_PLAYER; /*pour det la première case de chaque joueur (J1 prem case est 1 par contre la 1e case du J2 eest 6)*/

    for (int i = start; i < start + PITS_PER_PLAYER; i++)
        total += g->pits[i];

    return total;
}


void awale_init(Awale *g)
{
    for (int i = 0; i < NB_PITS; i++)
        g->pits[i] = INITIAL_SEEDS;

    g->score[0] = 0;
    g->score[1] = 0;
    g->current = 0;
}

/* Afficher le plateau */
void awale_print(const Awale *g)
{
    printf("\n========================================\n");
    printf("               AWALE GAME\n");
    printf("========================================\n");

    printf("\n Joueur 2 (haut) - Score : %d\n", g->score[1]);

    printf("       6     5     4     3     2     1\n");
    printf("    +-----+-----+-----+-----+-----+-----+\n");
    printf("    |");

    for (int i = 11; i >= 6; i--)
        printf(" %2d  |", g->pits[i]);

    printf("\n    +-----+-----+-----+-----+-----+-----+\n");
    printf("    |");

    for (int i = 0; i < 6; i++)
        printf(" %2d  |", g->pits[i]);

    printf("\n    +-----+-----+-----+-----+-----+-----+\n");
    printf("       1     2     3     4     5     6\n");

    printf(" Joueur 1 (bas) - Score : %d\n", g->score[0]);

    printf("\n Au tour du joueur %d.\n", g->current + 1);
    printf("========================================\n");
}

/*pour vérifier si les coups sont légals ou paq on retourne le vide si ils ne sont pas légales*/
static int apply_move(Awale *g, int pit_number)
{
    int player = g->current;
    int opponent = 1 - player;

    if (pit_number < 1 || pit_number > 6)
        return 0;

    int start = player * PITS_PER_PLAYER + pit_number - 1;

    if (g->pits[start] == 0)
        return 0;

    int opponent_seeds_before = count_seeds(g, opponent);

    /* Retirer les graines de la case choisie */
    int seeds = g->pits[start];
    g->pits[start] = 0;

    int position = start;

    /* Distribuer les graines */
    while (seeds > 0)
    {
        position = (position + 1) % NB_PITS;

        /* Ne pas redéposer dans la case de départ */
        if (position == start)
            continue;

        g->pits[position]++;
        seeds--;
    }

    /* Vérifier que l'adversaire est nourri */
    if (opponent_seeds_before == 0 &&
        count_seeds(g, opponent) == 0)
    {
        return 0;
    }

    /* Capturer les graines */
    int opponent_start = opponent * PITS_PER_PLAYER;
    int opponent_end = opponent_start + PITS_PER_PLAYER - 1;

    if (position >= opponent_start && position <= opponent_end)
    {
        int capture_position = position;
        int captured = 0;

        /* Vérifier les cases à capturer */
        while (capture_position >= opponent_start &&
               capture_position <= opponent_end &&
               (g->pits[capture_position] == 2 ||
                g->pits[capture_position] == 3))
        {
            captured += g->pits[capture_position];
            capture_position--;
        }

        /*
         * Ne pas capturer toutes les graines
         * de l'adversaire.
         */
        if (captured < count_seeds(g, opponent))
        {
            capture_position = position;

            while (capture_position >= opponent_start &&
                   capture_position <= opponent_end &&
                   (g->pits[capture_position] == 2 ||
                    g->pits[capture_position] == 3))
            {
                g->score[player] += g->pits[capture_position];
                g->pits[capture_position] = 0;
                capture_position--;
            }
        }
    }

    /* Passer au joueur suivant */
    g->current = opponent;

    return 1;
}

/* Jouer un coup */
int awale_play_move(Awale *g, int pit_number)
{
    Awale temp = *g;

    if (!apply_move(&temp, pit_number))
        return 0;

    *g = temp;

    return 1;
}

/* Vérifier si un joueur possède un coup légal */
int awale_has_legal_move(const Awale *g)
{
    Awale temp;

    for (int i = 1; i <= PITS_PER_PLAYER; i++)
    {
        temp = *g;

        if (apply_move(&temp, i))
            return 1;
    }

    return 0;
}

/*vérifier si la partie est terminée*/
int awale_is_game_over(const Awale *g)
{
    if (g->score[0] >= 25 || g->score[1] >= 25)
        return 1;

    if (!awale_has_legal_move(g))
        return 1;

    return 0;
}

/*distribution des graines restantes à leurs propriétaires*/
void awale_collect_remaining(Awale *g)
{
    for (int i = 0; i < PITS_PER_PLAYER; i++)
    {
        g->score[0] += g->pits[i];
        g->score[1] += g->pits[i + PITS_PER_PLAYER];

        g->pits[i] = 0;
        g->pits[i + PITS_PER_PLAYER] = 0;
    }
}

/* Sauvegarder la partie */
int awale_save(const Awale *g, const char *filename)
{
    FILE *file = fopen(filename, "w");

    if (file == NULL)
        return 0;

    fprintf(file, "%d %d %d\n",
            g->score[0], g->score[1], g->current);

    for (int i = 0; i < NB_PITS; i++)
        fprintf(file, "%d ", g->pits[i]);

    fprintf(file, "\n");

    fclose(file);

    return 1;
}

/* Charger une partie */
int awale_load(Awale *g, const char *filename)
{
    FILE *file = fopen(filename, "r");

    if (file == NULL)
        return 0;

    Awale temp;

    if (fscanf(file, "%d %d %d",
               &temp.score[0],
               &temp.score[1],
               &temp.current) != 3)
    {
        fclose(file);
        return 0;
    }

    if (temp.score[0] < 0 || temp.score[1] < 0 ||
        temp.current < 0 || temp.current > 1)
    {
        fclose(file);
        return 0;
    }

    int total = temp.score[0] + temp.score[1];

    for (int i = 0; i < NB_PITS; i++)
    {
        if (fscanf(file, "%d", &temp.pits[i]) != 1 ||
            temp.pits[i] < 0)
        {
            fclose(file);
            return 0;
        }

        total += temp.pits[i];
    }

    fclose(file);

    /* Vérifier que les 48 graines sont présentes */
    if (total != NB_PITS * INITIAL_SEEDS)
        return 0;

    *g = temp;

    return 1;
}
