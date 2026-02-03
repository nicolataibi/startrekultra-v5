/* 
 * STARTREK ULTRA - 3D LOGIC ENGINE 
 * Authors: Nicola Taibi, Supported by Google Gemini
 * Copyright (C) 2026 Nicola Taibi
 * License: GNU General Public License v3.0
 */

#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>

#pragma pack(push, 1)

#define MAX_OBJECTS 200
#define MAX_BEAMS 10
#define SHM_NAME "/startrek_ultra_shm"

/* 
 * Shared State Structure
 * Replaces the textual format of /tmp/ultra_map.dat
 */

typedef struct {
    float shm_x, shm_y, shm_z;
    float h, m;
    int type; /* 1=Player, 3=Base, 4=Star, 5=Planet, 6=BH, 10+=Enemies */
    int ship_class;
    int active;
    int health_pct;
    int energy;
    int faction;
    int id;
    char shm_name[64];
} SharedObject;

typedef struct {
    float shm_tx, shm_ty, shm_tz;
    int active;
} SharedBeam;

typedef struct {
    float shm_x, shm_y, shm_z;
    int active;
} SharedPoint;

typedef struct {
    float shm_x, shm_y, shm_z;
    int species;
    int active;
} SharedDismantle;

typedef struct {
    pthread_mutex_t mutex;
    sem_t data_ready;
    
    /* UI Info */
    int shm_energy;
    int shm_crew;
    int shm_torpedoes;
    int shm_shields[6];
    int shm_cargo_energy;
    int shm_cargo_torpedoes;
    int inventory[8];
    float shm_system_health[10];
    float shm_power_dist[3];
    int shm_tube_state;
    float shm_phaser_charge;
    float shm_life_support;
    int shm_corbomite;
    int shm_lock_target;
    int klingons;
    char quadrant[128];
    int shm_show_axes;
    int shm_show_grid;
    int shm_show_map;
    int is_cloaked;
    int shm_crypto_algo;
    int shm_q[3];
    float shm_s[3];
    int64_t shm_galaxy[11][11][11];
    
    /* Subspace Telemetry Metrics */
    float net_kbps;
    float net_efficiency;
    float net_jitter;
    float net_integrity;
    int net_last_packet_size;
    int net_avg_packet_size;
    int net_packet_count;
    long net_uptime;

    int object_count;
    SharedObject objects[MAX_OBJECTS];

    /* FX */
    int beam_count;
    SharedBeam beams[MAX_BEAMS];
    
    SharedPoint torp;
    SharedPoint boom;
    SharedPoint wormhole;
    SharedPoint jump_arrival;
    SharedPoint supernova_pos;
    int shm_sn_q[3];
    SharedDismantle dismantle;
    
    /* Synchronization counter */
    long long frame_id;
} GameState;

#pragma pack(pop)

#endif
