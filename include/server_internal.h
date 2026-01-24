/* 
 * STARTREK ULTRA - 3D LOGIC ENGINE 
 * Authors: Nicola Taibi, Supported by Google Gemini
 * Copyright (C) 2026 Nicola Taibi
 * License: GNU General Public License v3.0
 */

#ifndef SERVER_INTERNAL_H
#define SERVER_INTERNAL_H

#include <stdbool.h>
#include <pthread.h>
#include "network.h"

typedef enum { 
    NAV_STATE_IDLE, 
    NAV_STATE_ALIGN, 
    NAV_STATE_WARP, 
    NAV_STATE_REALIGN, 
    NAV_STATE_IMPULSE, 
    NAV_STATE_CHASE,
    NAV_STATE_ALIGN_IMPULSE
} NavState;

typedef struct {
    int socket;
    char name[64];
    int faction;
    int ship_class;
    int active;
    double gx, gy, gz; /* Absolute Galactic Position */
    
    /* Navigation State */
    NavState nav_state;
    int nav_timer; 
    double start_h, start_m;
    double target_h, target_m;
    double target_gx, target_gy, target_gz;
    double dx, dy, dz;
    double warp_speed;

    /* Combat State */
    bool torp_active;
    int torp_target;
    double tx, ty, tz;
    double tdx, tdy, tdz;
    StarTrekGame state; 
} ConnectedPlayer;

typedef enum {
    AI_STATE_PATROL = 0,
    AI_STATE_CHASE,
    AI_STATE_FLEE
} AIState;

typedef struct { int id, faction, q1, q2, q3; double x, y, z; int active; } NPCStar;
typedef struct { int id, q1, q2, q3; double x, y, z; int active; } NPCBlackHole;
typedef struct { 
    int id, faction, q1, q2, q3; 
    double x, y, z, h, m; 
    double gx, gy, gz; /* Absolute Galactic Coordinates 0-100 */
    int energy, active; 
    float engine_health; 
    int fire_cooldown; 
    AIState ai_state; 
    int target_player_idx; 
    int nav_timer; 
    double dx, dy, dz; 
} NPCShip;
typedef struct { int id, q1, q2, q3; double x, y, z; int resource_type, amount, active; } NPCPlanet;
typedef struct { int id, faction, q1, q2, q3; double x, y, z; int health, active; } NPCBase;

#define MAX_NPC 600
#define MAX_PLANETS 400
#define MAX_BASES 100
#define MAX_STARS 1200
#define MAX_BH 100

/* Local Quadrant Limits for Spatial Index (Optimization) */
#define MAX_Q_NPC 32
#define MAX_Q_PLANETS 16
#define MAX_Q_BASES 8
#define MAX_Q_STARS 32
#define MAX_Q_BH 4
#define MAX_Q_PLAYERS 32

/* Global Data accessed by modules */
extern NPCStar stars_data[MAX_STARS];
extern NPCBlackHole black_holes[MAX_BH];
extern NPCPlanet planets[MAX_PLANETS];
extern NPCBase bases[MAX_BASES];
extern NPCShip npcs[MAX_NPC];
extern ConnectedPlayer players[MAX_CLIENTS];
extern StarTrekGame galaxy_master;
extern pthread_mutex_t game_mutex;
extern int g_debug;

#define LOG_DEBUG(...) do { if (g_debug) { printf("DEBUG: " __VA_ARGS__); fflush(stdout); } } while (0)

#define GALAXY_VERSION 20260125

/* Spatial Partitioning Index */
typedef struct {
    NPCShip *npcs[MAX_Q_NPC];
    int npc_count;
    NPCPlanet *planets[MAX_Q_PLANETS];
    int planet_count;
    int static_planet_count; /* Marker for optimization */
    NPCBase *bases[MAX_Q_BASES];
    int base_count;
    int static_base_count;   /* Marker for optimization */
    NPCStar *stars[MAX_Q_STARS];
    int star_count;
    int static_star_count;   /* Marker for optimization */
    NPCBlackHole *black_holes[MAX_Q_BH];
    int bh_count;
    int static_bh_count;     /* Marker for optimization */
    ConnectedPlayer *players[MAX_Q_PLAYERS];
    int player_count;
} QuadrantIndex;

extern QuadrantIndex (*spatial_index)[11][11];
void rebuild_spatial_index();
void init_static_spatial_index();

#define IS_Q_VALID(q1,q2,q3) ((q1)>=1 && (q1)<=10 && (q2)>=1 && (q2)<=10 && (q3)>=1 && (q3)<=10)

/* Function Prototypes */
void normalize_upright(double *h, double *m);
void generate_galaxy();
int load_galaxy();
void save_galaxy();
const char* get_species_name(int s);

void broadcast_message(PacketMessage *msg);
void send_server_msg(int p_idx, const char *from, const char *text);

void process_command(int p_idx, const char *cmd);
void update_game_logic();

int read_all(int fd, void *buf, size_t len);
int write_all(int fd, const void *buf, size_t len);

#endif