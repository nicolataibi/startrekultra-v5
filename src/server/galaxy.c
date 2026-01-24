/* 
 * STARTREK ULTRA - 3D LOGIC ENGINE 
 * Authors: Nicola Taibi, Supported by Google Gemini
 * Copyright (C) 2026 Nicola Taibi
 * License: GNU General Public License v3.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "server_internal.h"

NPCStar stars_data[MAX_STARS];
NPCBlackHole black_holes[MAX_BH];
NPCPlanet planets[MAX_PLANETS];
NPCBase bases[MAX_BASES];
NPCShip npcs[MAX_NPC];
ConnectedPlayer players[MAX_CLIENTS];
StarTrekGame galaxy_master;

QuadrantIndex (*spatial_index)[11][11] = NULL;

void init_static_spatial_index() {
    if (!spatial_index) {
        spatial_index = calloc(11 * 11 * 11, sizeof(QuadrantIndex));
        if (!spatial_index) { perror("Failed to allocate spatial_index"); exit(1); }
    }
    memset(spatial_index, 0, 11 * 11 * 11 * sizeof(QuadrantIndex));

    for(int p=0; p<MAX_PLANETS; p++) if(planets[p].active) {
        if (!IS_Q_VALID(planets[p].q1, planets[p].q2, planets[p].q3)) continue;
        QuadrantIndex *q = &spatial_index[planets[p].q1][planets[p].q2][planets[p].q3];
        if (q->planet_count < MAX_Q_PLANETS) { 
            q->planets[q->planet_count++] = &planets[p]; 
            q->static_planet_count = q->planet_count;
        }
    }
    for(int b=0; b<MAX_BASES; b++) if(bases[b].active) {
        if (!IS_Q_VALID(bases[b].q1, bases[b].q2, bases[b].q3)) continue;
        QuadrantIndex *q = &spatial_index[bases[b].q1][bases[b].q2][bases[b].q3];
        if (q->base_count < MAX_Q_BASES) { 
            q->bases[q->base_count++] = &bases[b]; 
            q->static_base_count = q->base_count;
        }
    }
    for(int s=0; s<MAX_STARS; s++) if(stars_data[s].active) {
        if (!IS_Q_VALID(stars_data[s].q1, stars_data[s].q2, stars_data[s].q3)) continue;
        QuadrantIndex *q = &spatial_index[stars_data[s].q1][stars_data[s].q2][stars_data[s].q3];
        if (q->star_count < MAX_Q_STARS) { 
            q->stars[q->star_count++] = &stars_data[s]; 
            q->static_star_count = q->star_count;
        }
    }
    for(int h=0; h<MAX_BH; h++) if(black_holes[h].active) {
        if (!IS_Q_VALID(black_holes[h].q1, black_holes[h].q2, black_holes[h].q3)) continue;
        QuadrantIndex *q = &spatial_index[black_holes[h].q1][black_holes[h].q2][black_holes[h].q3];
        if (q->bh_count < MAX_Q_BH) { 
            q->black_holes[q->bh_count++] = &black_holes[h]; 
            q->static_bh_count = q->bh_count;
        }
    }
}

void rebuild_spatial_index() {
    if (!spatial_index) { init_static_spatial_index(); }
    
    /* Optimization: Reset only dynamic object counts instead of full memset */
    for(int i=1; i<=10; i++)
        for(int j=1; j<=10; j++)
            for(int l=1; l<=10; l++) {
                QuadrantIndex *q = &spatial_index[i][j][l];
                q->npc_count = 0;
                q->player_count = 0;
                /* Static objects are preserved, their counts are already set to static_*_count */
                q->planet_count = q->static_planet_count;
                q->base_count = q->static_base_count;
                q->star_count = q->static_star_count;
                q->bh_count = q->static_bh_count;
            }

    for(int n=0; n<MAX_NPC; n++) if(npcs[n].active) {
        if (!IS_Q_VALID(npcs[n].q1, npcs[n].q2, npcs[n].q3)) continue;
        QuadrantIndex *q = &spatial_index[npcs[n].q1][npcs[n].q2][npcs[n].q3];
        if (q->npc_count < MAX_Q_NPC) { q->npcs[q->npc_count++] = &npcs[n]; }
    }
    for(int u=0; u<MAX_CLIENTS; u++) if(players[u].active && players[u].name[0] != '\0') {
        if (!IS_Q_VALID(players[u].state.q1, players[u].state.q2, players[u].state.q3)) continue;
        QuadrantIndex *q = &spatial_index[players[u].state.q1][players[u].state.q2][players[u].state.q3];
        if (q->player_count < MAX_Q_PLAYERS) { q->players[q->player_count++] = &players[u]; }
    }
}

void save_galaxy() {
    FILE *f = fopen("galaxy.dat", "wb");
    if (!f) { perror("Failed to open galaxy.dat for writing"); return; }
    int version = GALAXY_VERSION;
    fwrite(&version, sizeof(int), 1, f);
    fwrite(&galaxy_master, sizeof(StarTrekGame), 1, f);
    fwrite(npcs, sizeof(NPCShip), MAX_NPC, f);
    fwrite(stars_data, sizeof(NPCStar), MAX_STARS, f);
    fwrite(black_holes, sizeof(NPCBlackHole), MAX_BH, f);
    fwrite(planets, sizeof(NPCPlanet), MAX_PLANETS, f);
    fwrite(bases, sizeof(NPCBase), MAX_BASES, f);
    fwrite(players, sizeof(ConnectedPlayer), MAX_CLIENTS, f);
    fclose(f);
}

int load_galaxy() {
    FILE *f = fopen("galaxy.dat", "rb");
    if (!f) return 0;
    int version;
    if (fread(&version, sizeof(int), 1, f) != 1 || version != GALAXY_VERSION) {
        printf("--- GALAXY VERSION MISMATCH OR CORRUPT FILE ---\n");
        fclose(f);
        return 0;
    }
    fread(&galaxy_master, sizeof(StarTrekGame), 1, f);
    fread(npcs, sizeof(NPCShip), MAX_NPC, f);
    fread(stars_data, sizeof(NPCStar), MAX_STARS, f);
    fread(black_holes, sizeof(NPCBlackHole), MAX_BH, f);
    fread(planets, sizeof(NPCPlanet), MAX_PLANETS, f);
    fread(bases, sizeof(NPCBase), MAX_BASES, f);
    fread(players, sizeof(ConnectedPlayer), MAX_CLIENTS, f);
    fclose(f);
    
    for(int i=0; i<MAX_CLIENTS; i++) {
        players[i].active = 0;
        players[i].socket = 0;
    }
    
    printf("--- PERSISTENT GALAXY LOADED SUCCESSFULLY ---\n");
    rebuild_spatial_index();
    return 1;
}

const char* get_species_name(int s) {
    switch(s) {
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
        case 4: return "Star"; case 5: return "Planet"; case 6: return "Black Hole";
        default: return "Unknown";
    }
}

void generate_galaxy() {
    printf("Generating Master Galaxy...\n");
    memset(&galaxy_master, 0, sizeof(StarTrekGame));
    memset(npcs, 0, sizeof(npcs));
    memset(stars_data, 0, sizeof(stars_data));
    memset(planets, 0, sizeof(planets));
    memset(bases, 0, sizeof(bases));
    memset(black_holes, 0, sizeof(black_holes));

    int n_count = 0, b_count = 0, p_count = 0, s_count = 0, bh_count = 0;
    
    for(int i=1; i<=10; i++)
        for(int j=1; j<=10; j++)
            for(int l=1; l<=10; l++) {
                int r = rand()%100;
                int kling = (r > 96) ? 3 : (r > 92) ? 2 : (r > 85) ? 1 : 0;
                int base = (rand()%100 > 98) ? 1 : 0;
                int planets_cnt = (rand()%100 > 90) ? (rand()%2 + 1) : 0;
                int star = (rand()%100 < 40) ? (rand()%3 + 1) : 0;
                int bh = (rand()%100 < 10) ? 1 : 0;
                
                int actual_k = 0, actual_b = 0, actual_p = 0, actual_s = 0, actual_bh = 0;
                
                for(int e=0; e<kling && n_count < MAX_NPC; e++) {
                    int faction = 10+(rand()%11);
                    int energy = 10000;
                    if (faction == FACTION_BORG) energy = 80000 + (rand()%20001);
                    else if (faction == FACTION_SPECIES_8472 || faction == FACTION_HIROGEN) energy = 60000 + (rand()%20001);
                    else if (faction == FACTION_KLINGON || faction == FACTION_ROMULAN || faction == FACTION_JEM_HADAR) energy = 30000 + (rand()%20001);
                    
                    NPCShip *n = &npcs[n_count];
                    n->id = n_count; n->faction = faction; n->active = 1;
                    n->q1 = i; n->q2 = j; n->q3 = l;
                    n->x = (rand()%100)/10.0; n->y = (rand()%100)/10.0; n->z = (rand()%100)/10.0;
                    n->gx = (i-1)*10.0 + n->x; n->gy = (j-1)*10.0 + n->y; n->gz = (l-1)*10.0 + n->z;
                    n->energy = energy; n->engine_health = 100.0f;
                    n->nav_timer = 60 + rand()%241; n->ai_state = AI_STATE_PATROL;
                    n_count++; actual_k++;
                }
                for(int b=0; b<base && b_count < MAX_BASES; b++) {
                    bases[b_count] = (NPCBase){.id=b_count, .faction=FACTION_FEDERATION, .q1=i, .q2=j, .q3=l, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .health=5000, .active=1}; b_count++; actual_b++;
                }
                for(int p=0; p<planets_cnt && p_count < MAX_PLANETS; p++) {
                    planets[p_count] = (NPCPlanet){.id=p_count, .q1=i, .q2=j, .q3=l, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .resource_type=(rand()%6)+1, .amount=1000, .active=1}; p_count++; actual_p++;
                }
                for(int s=0; s<star && s_count < MAX_STARS; s++) {
                    stars_data[s_count] = (NPCStar){.id=s_count, .faction=4, .q1=i, .q2=j, .q3=l, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .active=1}; s_count++; actual_s++;
                }
                for(int h=0; h<bh && bh_count < MAX_BH; h++) {
                    black_holes[bh_count] = (NPCBlackHole){.id=bh_count, .q1=i, .q2=j, .q3=l, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .active=1}; bh_count++; actual_bh++;
                }

                /* Cap values to 9 for BPNBS encoding */
                int c_bh = actual_bh > 9 ? 9 : actual_bh;
                int c_p = actual_p > 9 ? 9 : actual_p;
                int c_k = actual_k > 9 ? 9 : actual_k;
                int c_b = actual_b > 9 ? 9 : actual_b;
                int c_s = actual_s > 9 ? 9 : actual_s;

                galaxy_master.g[i][j][l] = c_bh * 10000 + c_p * 1000 + c_k * 100 + c_b * 10 + c_s;
                galaxy_master.k9 += actual_k;
                galaxy_master.b9 += actual_b;
            }
    printf("Galaxy generated: %d NPCs, %d Stars, %d Planets, %d Bases, %d Black Holes.\n", n_count, s_count, p_count, b_count, bh_count);
}