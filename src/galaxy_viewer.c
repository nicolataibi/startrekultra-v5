/* 
 * STARTREK ULTRA - Galaxy Data Viewer
 * Authors: Nicola Taibi, Supported by Google Gemini
 * Copyright (C) 2026 Nicola Taibi
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/game_state.h"
#include "../include/server_internal.h"
#include "../include/network.h"

StarTrekGame galaxy_master;
NPCShip npcs[MAX_NPC];
NPCStar stars_data[MAX_STARS];
NPCBlackHole black_holes[MAX_BH];
NPCPlanet planets[MAX_PLANETS];
NPCBase bases[MAX_BASES];
ConnectedPlayer players[MAX_CLIENTS];

const char* get_faction_name(int faction) {
    switch(faction) {
        case FACTION_FEDERATION: return "Federation";
        case FACTION_KLINGON:    return "Klingon";
        case FACTION_ROMULAN:    return "Romulan";
        case FACTION_BORG:       return "Borg";
        case FACTION_CARDASSIAN: return "Cardassian";
        case FACTION_JEM_HADAR:  return "Jem'Hadar";
        case FACTION_THOLIAN:    return "Tholian";
        case FACTION_GORN:       return "Gorn";
        case FACTION_FERENGI:    return "Ferengi";
        case FACTION_SPECIES_8472: return "Species 8472";
        case FACTION_BREEN:      return "Breen";
        case FACTION_HIROGEN:    return "Hirogen";
        default: return "Unknown";
    }
}

void print_help() {
    printf("Usage: ./trek_galaxy_viewer [command]\n");
    printf("Commands:\n");
    printf("  stats             Show global galaxy statistics\n");
    printf("  map <q3>          Show a 2D map slice for Z quadrant q3\n");
    printf("  list <q1> <q2> <q3>  List objects in quadrant (1-10)\n");
    printf("  players           List all persistent players\n");
    printf("  search <name>     Search for a player or ship by name\n");
}

int main(int argc, char *argv[]) {
    FILE *f = fopen("galaxy.dat", "rb");
    if (!f) {
        perror("Could not open galaxy.dat");
        return 1;
    }

    int version;
    if (fread(&version, sizeof(int), 1, f) != 1) {
        fprintf(stderr, "Failed to read version\n");
        fclose(f);
        return 1;
    }

    if (version != GALAXY_VERSION) {
        printf("Warning: Version mismatch. File: %d, Expected: %d\n", version, GALAXY_VERSION);
    }

    fread(&galaxy_master, sizeof(StarTrekGame), 1, f);
    fread(npcs, sizeof(NPCShip), MAX_NPC, f);
    fread(stars_data, sizeof(NPCStar), MAX_STARS, f);
    fread(black_holes, sizeof(NPCBlackHole), MAX_BH, f);
    fread(planets, sizeof(NPCPlanet), MAX_PLANETS, f);
    fread(bases, sizeof(NPCBase), MAX_BASES, f);
    fread(players, sizeof(ConnectedPlayer), MAX_CLIENTS, f);
    fclose(f);

    if (argc < 2) {
        print_help();
        return 0;
    }

    if (strcmp(argv[1], "stats") == 0) {
        int n_active = 0, s_active = 0, b_active = 0, p_active = 0, bh_active = 0;
        for(int i=0; i<MAX_NPC; i++) if(npcs[i].active) n_active++;
        for(int i=0; i<MAX_STARS; i++) if(stars_data[i].active) s_active++;
        for(int i=0; i<MAX_BASES; i++) if(bases[i].active) b_active++;
        for(int i=0; i<MAX_PLANETS; i++) if(planets[i].active) p_active++;
        for(int i=0; i<MAX_BH; i++) if(black_holes[i].active) bh_active++;

        printf("--- Galaxy Statistics ---\n");
        printf("Version: %d\n", version);
        printf("Total NPCs: %d\n", n_active);
        printf("Total Stars: %d\n", s_active);
        printf("Total Bases: %d\n", b_active);
        printf("Total Planets: %d\n", p_active);
        printf("Total Black Holes: %d\n", bh_active);
        printf("Galaxy Master K9: %d, B9: %d\n", galaxy_master.k9, galaxy_master.b9);
    } 
    else if (strcmp(argv[1], "map") == 0 && argc == 3) {
        int q3 = atoi(argv[2]);
        if (q3 < 1 || q3 > 10) {
            printf("Invalid Z quadrant (1-10)\n");
            return 1;
        }
        printf("--- Galaxy Map Slice (Z=%d) ---\n", q3);
        printf("    1  2  3  4  5  6  7  8  9  10 (X)\n");
        for(int j=1; j<=10; j++) {
            printf("%2d ", j);
            for(int i=1; i<=10; i++) {
                int bpnbs = galaxy_master.g[i][j][q3];
                int k = (bpnbs/100)%10;
                int b = (bpnbs/10)%10;
                int s = bpnbs%10;
                
                if (k > 0) printf(" K ");
                else if (b > 0) printf(" B ");
                else if (s > 0) printf(" * ");
                else printf(" . ");
            }
            printf(" (Y:%d)\n", j);
        }
    }
    else if (strcmp(argv[1], "list") == 0 && argc == 5) {
        int q1 = atoi(argv[2]);
        int q2 = atoi(argv[3]);
        int q3 = atoi(argv[4]);
        
        if (!IS_Q_VALID(q1, q2, q3)) {
            printf("Invalid quadrant coordinates (1-10)\n");
            return 1;
        }

        printf("--- Objects in Quadrant [%d,%d,%d] ---\n", q1, q2, q3);
        
        int bpnbs = galaxy_master.g[q1][q2][q3];
        printf("BPNBS Encoding: %05d (BH: %d, P: %d, K: %d, B: %d, S: %d)\n", 
               bpnbs, bpnbs/10000, (bpnbs/1000)%10, (bpnbs/100)%10, (bpnbs/10)%10, bpnbs%10);

        for(int i=0; i<MAX_NPC; i++) if(npcs[i].active && npcs[i].q1 == q1 && npcs[i].q2 == q2 && npcs[i].q3 == q3)
            printf("[NPC] ID:%d Faction:%s Coord:%.1f,%.1f,%.1f Energy:%d\n", npcs[i].id, get_faction_name(npcs[i].faction), npcs[i].x, npcs[i].y, npcs[i].z, npcs[i].energy);
        
        for(int i=0; i<MAX_BASES; i++) if(bases[i].active && bases[i].q1 == q1 && bases[i].q2 == q2 && bases[i].q3 == q3)
            printf("[BASE] ID:%d Faction:%s Coord:%.1f,%.1f,%.1f Health:%d\n", bases[i].id, get_faction_name(bases[i].faction), bases[i].x, bases[i].y, bases[i].z, bases[i].health);

        for(int i=0; i<MAX_PLANETS; i++) if(planets[i].active && planets[i].q1 == q1 && planets[i].q2 == q2 && planets[i].q3 == q3)
            printf("[PLANET] ID:%d Type:%d Coord:%.1f,%.1f,%.1f Resources:%d\n", planets[i].id, planets[i].resource_type, planets[i].x, planets[i].y, planets[i].z, planets[i].amount);

        for(int i=0; i<MAX_STARS; i++) if(stars_data[i].active && stars_data[i].q1 == q1 && stars_data[i].q2 == q2 && stars_data[i].q3 == q3)
            printf("[STAR] ID:%d Coord:%.1f,%.1f,%.1f\n", stars_data[i].id, stars_data[i].x, stars_data[i].y, stars_data[i].z);

        for(int i=0; i<MAX_BH; i++) if(black_holes[i].active && black_holes[i].q1 == q1 && black_holes[i].q2 == q2 && black_holes[i].q3 == q3)
            printf("[BLACK HOLE] ID:%d Coord:%.1f,%.1f,%.1f\n", black_holes[i].id, black_holes[i].x, black_holes[i].y, black_holes[i].z);
    }
    else if (strcmp(argv[1], "players") == 0) {
        printf("--- Persistent Players ---\n");
        for(int i=0; i<MAX_CLIENTS; i++) {
            if (players[i].name[0] != '\0') {
                printf("Name: %-15s Faction: %-12s Pos: [%d,%d,%d] (%.1f,%.1f,%.1f)\n", 
                       players[i].name, get_faction_name(players[i].faction),
                       players[i].state.q1, players[i].state.q2, players[i].state.q3,
                       players[i].state.s1, players[i].state.s2, players[i].state.s3);
            }
        }
    }
    else if (strcmp(argv[1], "search") == 0 && argc == 3) {
        const char *name = argv[2];
        printf("Searching for '%s'...\n", name);
        for(int i=0; i<MAX_CLIENTS; i++) {
            if (players[i].name[0] != '\0' && strcasestr(players[i].name, name)) {
                printf("[PLAYER] Found: %s in Quadrant [%d,%d,%d]\n", players[i].name, players[i].state.q1, players[i].state.q2, players[i].state.q3);
            }
        }
    }
    else {
        print_help();
    }

    return 0;
}
