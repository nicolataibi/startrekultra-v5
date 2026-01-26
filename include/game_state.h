/* 
 * STARTREK ULTRA - 3D LOGIC ENGINE 
 * Authors: Nicola Taibi, Supported by Google Gemini
 * Copyright (C) 2026 Nicola Taibi
 * License: GNU General Public License v3.0
 */

#ifndef GAME_STATE_H
#define GAME_STATE_H

#include <stdbool.h>
#include <stdint.h>

#pragma pack(push, 1)

#define MAX_NET_OBJECTS 128
#define MAX_NET_BEAMS 8

typedef struct {
    float net_tx, net_ty, net_tz;
    int active;
} NetBeam;

typedef struct {
    float net_x, net_y, net_z;
    float h, m;
    int type;       /* 1=Player, 3=Base, 4=Star, 5=Planet, etc */
    int ship_class; /* Specifica il modello 3D (es. Galaxy, Constitution) */
    int active;
    int health_pct; /* 0-100% Health/Energy status for HUD */
    int id;         /* Universal Target ID */
    char name[64];  /* Captain name or ship name */
} NetObject;

typedef struct {
    float net_x, net_y, net_z;
    int active;
} NetPoint;

typedef struct {
    float net_x, net_y, net_z;
    int species;
    int active;
} NetDismantle;

typedef struct {
    /* Galaxy Data - Moved to TOP for reliable alignment and sync */
    int g[11][11][11];          /* The Galaxy Cube (BPNBS Encoding) */
    int z[11][11][11];          /* Scanned Map Cube */

    /* Coordinates */
    int q1, q2, q3;             /* Quadrant Position (X, Y, Z) */
    int old_q1, old_q2, old_q3; /* Persistence tracking */
    double s1, s2, s3;          /* Sector Position (X, Y, Z) */

    /* Metadata and Totals */
    int k9, b9;
    long long frame_id;
    char captain_name[64];

    /* Resources & Status */
    int energy;
    int torpedoes;
    int cargo_energy;
    int cargo_torpedoes;
    int crew_count;
    int inventory[7];
    int species_counts[11];
    int shields[6];
    
    /* Current Quadrant counts */
    int k3, b3, st3, p3, bh3;
    
    /* Ship Systems */
    double ent_h, ent_m;
    int lock_target;
    float power_dist[3];
    uint8_t is_playing_dead;
    uint8_t is_cloaked;
    float system_health[8];
    float life_support;
    
    /* Time & Meta */
    double t, t0;
    int t9;
    int corbomite_count;

    /* Visual preferences */
    uint8_t show_axes;
    uint8_t show_grid;

    /* Multi-user sync (Objects in current sector) */
    int object_count;
    NetObject objects[MAX_NET_OBJECTS];
    int beam_count;
    NetBeam beams[MAX_NET_BEAMS];
    NetPoint torp;
    NetPoint boom;
    NetPoint wormhole;
    NetDismantle dismantle;
} StarTrekGame;

#pragma pack(pop)

#endif
