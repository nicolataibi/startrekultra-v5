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

/* Magic Signature for Key Verification (32 bytes) */
#define HANDSHAKE_MAGIC_STRING "TREK-ULTRA-KEY-VERIFICATION-SIG"

#define CRYPTO_NONE 0
#define CRYPTO_AES  1
#define CRYPTO_CHACHA 2
#define CRYPTO_ARIA 3
#define CRYPTO_CAMELLIA 4
#define CRYPTO_SEED     5
#define CRYPTO_CAST5    6
#define CRYPTO_IDEA     7
#define CRYPTO_3DES     8
#define CRYPTO_BLOWFISH 9
#define CRYPTO_RC4      10
#define CRYPTO_DES      11
#define CRYPTO_PQC      12

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
    int32_t type;
    char name[64];
    int32_t faction;
    int32_t ship_class;
} PacketLogin;

typedef struct {
    int32_t type;
    char cmd[256];
} PacketCommand;

typedef struct {
    int32_t type;
    int32_t pubkey_len;
    uint8_t pubkey[256]; /* Standard EC Public Key */
} PacketHandshake;

typedef struct {
    int32_t type;
    char from[64];
    int32_t faction;
    int32_t scope; /* 0: Global, 1: Faction, 2: Private */
    int32_t target_id; /* Player ID (1-based) for Private Message */
    int32_t length;
    int64_t origin_frame; /* Server frame used for frequency scrambling */
    uint8_t is_encrypted;
    uint8_t crypto_algo; /* 1:AES... 11:DES, 12:PQC (ML-KEM/Kyber) */
    uint8_t iv[12];      /* GCM/Poly/CTR/CBC IV */
    uint8_t tag[16];     /* Auth Tag */
    uint8_t has_signature;
    uint8_t signature[64]; /* Ed25519 Signature */
    uint8_t sender_pubkey[32]; 
    char text[65536];
} PacketMessage;

/* Update Packet: Optimized for variable length transmission */
typedef struct {
    int32_t type;
    int64_t frame_id;
    int32_t q1, q2, q3;
    float s1, s2, s3;
    float ent_h, ent_m;
    int32_t energy;
    int32_t torpedoes;
    int32_t cargo_energy;
    int32_t cargo_torpedoes;
    int32_t crew_count;
    int32_t shields[6];
    int32_t inventory[8];
    float system_health[10];
    float power_dist[3];
    float life_support;
    int32_t corbomite_count;
    int32_t lock_target;
    int32_t tube_state;
    float phaser_charge;
    uint8_t is_cloaked;
    uint8_t encryption_enabled;
    NetPoint torp;
    NetPoint boom;
    NetPoint wormhole;
    NetPoint jump_arrival;
    NetDismantle dismantle;
    NetPoint supernova_pos; 
    int32_t supernova_q[3];
    int32_t beam_count;
    NetBeam beams[MAX_NET_BEAMS];
    int64_t map_update_val;
    int32_t map_update_q[3];
    int32_t object_count;
    NetObject objects[MAX_NET_OBJECTS];
} PacketUpdate;

#pragma pack(pop)

#endif
