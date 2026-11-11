#include <stdio.h>
#include "awale.h"

int main(void)
{
    Awale g;
    char command;
    char filename[] = "awale_save.txt";

    awale_init(&g);

    printf("Bienvenue dans le jeu Awale !\n");

    while (1)
    {
        awale_print(&g);

if (awale_is_game_over(&g))
{
    printf("\nLa partie est terminee !\n");

    printf("\nScores finaux :\n");
    printf("Joueur 1 : %d points\n", g.score[0]);
    printf("Joueur 2 : %d points\n", g.score[1]);

    if (g.score[0] >= 25)
        printf("\nLe joueur 1 est le vainqueur !\n");
    else
        printf("\nLe joueur 2 est le vainqueur !\n");

    break;
}
        printf("\n========== MENU ==========\n");
        printf("1 a 6 : Choisir une case\n");
        printf("s : Sauvegarder\n");
        printf("l : Charger\n");
        printf("q : Quitter\n");
        printf("==========================\n");

        printf("Votre choix : ");

        if (scanf(" %c", &command) != 1)
            break;

        if (command >= '1' && command <= '6')
        {
            int choice = command - '0';

            if (!awale_play_move(&g, choice))
                printf("Coup invalide ! Reessayez.\n");
        }
        else if (command == 's')
        {
            if (awale_save(&g, filename))
                printf("Partie sauvegardee !\n");
            else
                printf("Erreur de sauvegarde.\n");
        }
        else if (command == 'l')
        {
            if (awale_load(&g, filename))
                printf("Partie chargee !\n");
            else
                printf("Erreur de chargement.\n");
        }
        else if (command == 'q')
        {
            printf("Fin du jeu.\n");
            break;
        }
        else
        {
            printf("Commande invalide !\n");
        }
    }

    return 0;
}