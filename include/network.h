/* 
 * STARTREK ULTRA - 3D LOGIC ENGINE 
 * Authors: Nicola Taibi, Supported by Google Gemini
 * Copyright (C) 2026 Nicola Taibi
 * License: GNU General Public License v3.0
 */

#ifndef NETWORK_H
#define NETWORK_H

#include "game_state.h"

#pragma pack(push, 1)

#define DEFAULT_PORT 5000
#define MAX_CLIENTS 32
#define PKT_LOGIN 1
#define PKT_COMMAND 2
#define PKT_UPDATE 3
#define PKT_MESSAGE 4
#define PKT_QUERY 5
#define PKT_HANDSHAKE 6

#define CRYPTO_NONE 0
#define CRYPTO_AES  1
#define CRYPTO_CHACHA 2

#define SCOPE_GLOBAL 0
#define SCOPE_FACTION 1
#define SCOPE_PRIVATE 2

typedef enum {
    FACTION_FEDERATION = 0,
    FACTION_KLINGON = 10,
    FACTION_ROMULAN = 11,
    FACTION_BORG = 12,
    FACTION_CARDASSIAN = 13,
    FACTION_JEM_HADAR = 14,
    FACTION_THOLIAN = 15,
    FACTION_GORN = 16,
    FACTION_FERENGI = 17,
    FACTION_SPECIES_8472 = 18,
    FACTION_BREEN = 19,
    FACTION_HIROGEN = 20
} Faction;

typedef enum {
    SHIP_CLASS_CONSTITUTION = 0,
    SHIP_CLASS_MIRANDA,
    SHIP_CLASS_EXCELSIOR,
    SHIP_CLASS_CONSTELLATION,
    SHIP_CLASS_DEFIANT,
    SHIP_CLASS_GALAXY,          /* Enterprise-D style */
    SHIP_CLASS_SOVEREIGN,       /* Enterprise-E style */
    SHIP_CLASS_INTREPID,        /* Voyager style */
    SHIP_CLASS_AKIRA,           /* Heavy Escort / Carrier */
    SHIP_CLASS_NEBULA,          /* Galaxy-variant with pod */
    SHIP_CLASS_AMBASSADOR,      /* Enterprise-C style */
    SHIP_CLASS_OBERTH,          /* Small Science Ship */
    SHIP_CLASS_STEAMRUNNER,     /* Specialized Escort */
    SHIP_CLASS_GENERIC_ALIEN
} ShipClass;

typedef struct {
    int type;
    char name[64];
    int faction;
    int ship_class;
} PacketLogin;

typedef struct {
    int type;
    char cmd[256];
} PacketCommand;

typedef struct {
    int type;
    int pubkey_len;
    uint8_t pubkey[256]; /* Standard EC Public Key */
} PacketHandshake;

typedef struct {
    int type;
    char from[64];
    int faction;
    int scope; /* 0: Global, 1: Faction, 2: Private */
    int target_id; /* Player ID (1-based) for Private Message */
    int length;
    long long origin_frame; /* Server frame used for frequency scrambling */
    uint8_t is_encrypted;
    uint8_t crypto_algo; /* 1: AES, 2: ChaCha */
    uint8_t iv[12];      /* GCM/Poly Standard IV */
    uint8_t tag[16];     /* Auth Tag */
    char text[65536];
} PacketMessage;

/* Update Packet: Optimized for variable length transmission */
typedef struct {
    int type;
    long long frame_id;
    int q1, q2, q3;
    double s1, s2, s3;
    double ent_h, ent_m;
    int energy;
    int torpedoes;
    int cargo_energy;
    int cargo_torpedoes;
    int crew_count;
    int shields[6];
    int inventory[7];
    float system_health[8];
    int lock_target;
    uint8_t is_cloaked;
    uint8_t encryption_enabled;
    NetPoint torp;
    NetPoint boom;
    NetPoint wormhole;
    NetPoint jump_arrival;
    NetDismantle dismantle;
    NetPoint supernova_pos; 
    int supernova_q[3];
    int beam_count;
    NetBeam beams[MAX_NET_BEAMS];
    long long map_update_val;
    int map_update_q[3];
    int object_count;
    NetObject objects[MAX_NET_OBJECTS];
} PacketUpdate;

#pragma pack(pop)

#endif
