/* 
 * STARTREK ULTRA - 3D LOGIC ENGINE 
 * Authors: Nicola Taibi, Supported by Google Gemini
 * Copyright (C) 2026 Nicola Taibi
 * License: GNU General Public License v3.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/socket.h>
#include "server_internal.h"
#include "ui.h"

/* Helper macro to safely append to a buffer with length check (used in handle_srs) */
#define SAFE_APPEND(buffer, format, ...) do { \
    char _tmp[512]; \
    snprintf(_tmp, sizeof(_tmp), format, ##__VA_ARGS__); \
    if (strlen(buffer) + strlen(_tmp) < 65536 - 100) { \
        strcat(buffer, _tmp); \
    } \
} while(0)

/* Type definition for command handlers */
typedef void (*CommandHandler)(int p_idx, const char *params);

void handle_help(int p_idx, const char *params);

typedef struct {
    const char *name;
    CommandHandler handler;
    const char *description;
} CommandDef;

void normalize_upright(double *h, double *m) {
    *h = fmod(*h, 360.0); if (*h < 0) *h += 360.0;
    while (*m > 180.0) *m -= 360.0; 
    while (*m < -180.0) *m += 360.0;
    if (*m > 90.0) { *m = 180.0 - *m; *h = fmod(*h + 180.0, 360.0); }
    else if (*m < -90.0) { *m = -180.0 - *m; *h = fmod(*h + 180.0, 360.0); }
}

/* --- Command Handlers --- */

void handle_enc(int i, const char *params) {
    if (strstr(params, "aes")) {
        players[i].crypto_algo = CRYPTO_AES;
        send_server_msg(i, "COMPUTER", "Subspace encryption: AES-256-GCM ACTIVE.");
    } else if (strstr(params, "chacha")) {
        players[i].crypto_algo = CRYPTO_CHACHA;
        send_server_msg(i, "COMPUTER", "Subspace encryption: CHACHA20-POLY1305 ACTIVE.");
    } else if (strstr(params, "aria")) {
        players[i].crypto_algo = CRYPTO_ARIA;
        send_server_msg(i, "COMPUTER", "Subspace encryption: ARIA-256-GCM ACTIVE.");
    } else if (strstr(params, "camellia")) {
        players[i].crypto_algo = CRYPTO_CAMELLIA;
        send_server_msg(i, "COMPUTER", "Subspace encryption: CAMELLIA-256-CTR (ROMULAN) ACTIVE.");
    } else if (strstr(params, "seed")) {
        players[i].crypto_algo = CRYPTO_SEED;
        send_server_msg(i, "COMPUTER", "Subspace encryption: SEED-CBC (ORION) ACTIVE.");
    } else if (strstr(params, "cast")) {
        players[i].crypto_algo = CRYPTO_CAST5;
        send_server_msg(i, "COMPUTER", "Subspace encryption: CAST5-CBC (OLD REPUBLIC) ACTIVE.");
    } else if (strstr(params, "idea")) {
        players[i].crypto_algo = CRYPTO_IDEA;
        send_server_msg(i, "COMPUTER", "Subspace encryption: IDEA-CBC (MAQUIS) ACTIVE.");
    } else if (strstr(params, "3des")) {
        players[i].crypto_algo = CRYPTO_3DES;
        send_server_msg(i, "COMPUTER", "Subspace encryption: DES-EDE3-CBC (ANCIENT) ACTIVE.");
    } else if (strstr(params, "bf") || strstr(params, "blowfish")) {
        players[i].crypto_algo = CRYPTO_BLOWFISH;
        send_server_msg(i, "COMPUTER", "Subspace encryption: BLOWFISH-CBC (FERENGI) ACTIVE.");
    } else if (strstr(params, "rc4")) {
        players[i].crypto_algo = CRYPTO_RC4;
        send_server_msg(i, "COMPUTER", "Subspace encryption: RC4-STREAM (TACTICAL) ACTIVE.");
    } else if (strstr(params, "des") && !strstr(params, "3des")) {
        players[i].crypto_algo = CRYPTO_DES;
        send_server_msg(i, "COMPUTER", "Subspace encryption: DES-CBC (PRE-WARP) ACTIVE.");
    } else if (strstr(params, "pqc") || strstr(params, "kyber")) {
        players[i].crypto_algo = CRYPTO_PQC;
        send_server_msg(i, "COMPUTER", "Subspace encryption: ML-KEM-1024 (POST-QUANTUM) ACTIVE.");
        send_server_msg(i, "SCIENCE", "Quantum Tunnel established. Communications are now immune to Shor's algorithm.");
    } else if (strstr(params, "off")) {
        players[i].crypto_algo = CRYPTO_NONE;
        send_server_msg(i, "COMPUTER", "WARNING: Encryption DISABLED. Signal is now RAW.");
    } else {
        send_server_msg(i, "COMPUTER", "Usage: enc aes | chacha | aria | camellia | seed | cast | idea | 3des | bf | rc4 | des | pqc | off");
    }
}

void handle_nav(int i, const char *params) {
    double h, m, w; 
    if (sscanf(params, "%lf %lf %lf", &h, &m, &w) == 3) {
        normalize_upright(&h, &m);
        players[i].target_h = h; players[i].target_m = m;
        players[i].start_h = players[i].state.ent_h; players[i].start_m = players[i].state.ent_m;
        double rad_h = h * M_PI / 180.0; double rad_m = m * M_PI / 180.0;
        players[i].dx = cos(rad_m) * sin(rad_h); players[i].dy = cos(rad_m) * -cos(rad_h); players[i].dz = sin(rad_m);
        players[i].target_gx = (players[i].state.q1-1)*10.0+players[i].state.s1+players[i].dx*w*10.0;
        players[i].target_gy = (players[i].state.q2-1)*10.0+players[i].state.s2+players[i].dy*w*10.0;
        players[i].target_gz = (players[i].state.q3-1)*10.0+players[i].state.s3+players[i].dz*w*10.0;
        players[i].nav_state = NAV_STATE_ALIGN; 
        
        double dh = players[i].target_h - players[i].state.ent_h;
        while(dh>180) dh-=360; 
        while(dh<-180) dh+=360;
        if(fabs(dh)<1.0 && fabs(players[i].target_m - players[i].state.ent_m)<1.0) players[i].nav_timer=10;
        else players[i].nav_timer = 60;
        
        send_server_msg(i, "HELMSMAN", "Course plotted. Aligning ship.");
    } else {
        send_server_msg(i, "COMPUTER", "Usage: nav <H> <M> <W>");
    }
}

void handle_imp(int i, const char *params) {
    double h, m, s;
    int args = sscanf(params, "%lf %lf %lf", &h, &m, &s);
    if (args == 1) {
        /* Speed update only */
        players[i].warp_speed = h / 200.0; if (players[i].warp_speed > 0.5) players[i].warp_speed = 0.5;
        char msg[64]; sprintf(msg, "Impulse adjusted to %.0f%%.", players[i].warp_speed * 200.0);
        send_server_msg(i, "HELMSMAN", msg);
        players[i].nav_state = NAV_STATE_IMPULSE;
    } else if (args == 3) {
        normalize_upright(&h, &m);
        players[i].target_h = h; players[i].target_m = m;
        players[i].start_h = players[i].state.ent_h; players[i].start_m = players[i].state.ent_m;
        double rad_h = h * M_PI / 180.0; double rad_m = m * M_PI / 180.0;
        players[i].dx = cos(rad_m) * sin(rad_h); players[i].dy = cos(rad_m) * -cos(rad_h); players[i].dz = sin(rad_m);
        players[i].warp_speed = s / 200.0; if (players[i].warp_speed > 0.5) players[i].warp_speed = 0.5;
        players[i].nav_state = NAV_STATE_ALIGN_IMPULSE;
        
        double dh = players[i].target_h - players[i].state.ent_h;
        while(dh>180) dh-=360; 
        while(dh<-180) dh+=360;
        if(fabs(dh)<1.0 && fabs(players[i].target_m - players[i].state.ent_m)<1.0) players[i].nav_timer=10;
        else players[i].nav_timer = 60;
        
        send_server_msg(i, "HELMSMAN", "Course plotted. Aligning ship.");
    } else {
        send_server_msg(i, "COMPUTER", "Usage: imp <H> <M> <S> or imp <S>");
    }
}

void handle_apr(int i, const char *params) {
    int tid; double tdist;
    if (sscanf(params, "%d %lf", &tid, &tdist) == 2) {
        double tx, ty, tz; bool found = false;
        int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
        
        /* 1. Players (Global) */
        if (tid >= 1 && tid <= 32) {
            int idx = tid - 1;
            if (players[idx].active) {
                tx = players[idx].gx; ty = players[idx].gy; tz = players[idx].gz;
                found = true;
            }
        }
        /* 2. NPCs (Global) */
        else if (tid >= 1000 && tid < 1000+MAX_NPC) {
            int idx = tid - 1000;
            if (npcs[idx].active) {
                tx = npcs[idx].gx; ty = npcs[idx].gy; tz = npcs[idx].gz;
                found = true;
            }
        }
        /* 3. Starbases (Local) */
        else if (tid >= 2000 && tid < 2000+MAX_BASES) {
            int idx = tid - 2000;
            if (bases[idx].active && bases[idx].q1==pq1 && bases[idx].q2==pq2 && bases[idx].q3==pq3) {
                tx = (bases[idx].q1-1)*10+bases[idx].x; ty = (bases[idx].q2-1)*10+bases[idx].y; tz = (bases[idx].q3-1)*10+bases[idx].z;
                found = true;
            }
        }
        /* 4. Planets (Local) */
        else if (tid >= 3000 && tid < 3000+MAX_PLANETS) {
            int idx = tid - 3000;
            if (planets[idx].active && planets[idx].q1==pq1 && planets[idx].q2==pq2 && planets[idx].q3==pq3) {
                tx = (planets[idx].q1-1)*10+planets[idx].x; ty = (planets[idx].q2-1)*10+planets[idx].y; tz = (planets[idx].q3-1)*10+planets[idx].z;
                found = true;
            }
        }
        /* 5. Stars (Local) */
        else if (tid >= 4000 && tid < 4000+MAX_STARS) {
            int idx = tid - 4000;
            if (stars_data[idx].active && stars_data[idx].q1==pq1 && stars_data[idx].q2==pq2 && stars_data[idx].q3==pq3) {
                tx = (stars_data[idx].q1-1)*10+stars_data[idx].x; ty = (stars_data[idx].q2-1)*10+stars_data[idx].y; tz = (stars_data[idx].q3-1)*10+stars_data[idx].z;
                found = true;
            }
        }
        /* 6. Black Holes (Local) */
        else if (tid >= 7000 && tid < 7000+MAX_BH) {
            int idx = tid - 7000;
            if (black_holes[idx].active && black_holes[idx].q1==pq1 && black_holes[idx].q2==pq2 && black_holes[idx].q3==pq3) {
                tx = (black_holes[idx].q1-1)*10+black_holes[idx].x; ty = (black_holes[idx].q2-1)*10+black_holes[idx].y; tz = (black_holes[idx].q3-1)*10+black_holes[idx].z;
                found = true;
            }
        }
        /* 7. Comets (Global) */
        else if (tid >= 10000 && tid < 10000+MAX_COMETS) {
            int idx = tid - 10000;
            if (comets[idx].active) {
                tx = (comets[idx].q1-1)*10+comets[idx].x; ty = (comets[idx].q2-1)*10+comets[idx].y; tz = (comets[idx].q3-1)*10+comets[idx].z;
                found = true;
            }
        }
        /* 8. Monsters (Global) */
        else if (tid >= 18000 && tid < 18000+MAX_MONSTERS) {
            int idx = tid - 18000;
            if (monsters[idx].active) {
                tx = (monsters[idx].q1-1)*10+monsters[idx].x; ty = (monsters[idx].q2-1)*10+monsters[idx].y; tz = (monsters[idx].q3-1)*10+monsters[idx].z;
                found = true;
            }
        }
        /* 9. Nebulas (Local) */
        else if (tid >= 8000 && tid < 8000+MAX_NEBULAS) {
            int idx = tid - 8000;
            if (nebulas[idx].active && nebulas[idx].q1==pq1 && nebulas[idx].q2==pq2 && nebulas[idx].q3==pq3) {
                tx = (nebulas[idx].q1-1)*10+nebulas[idx].x; ty = (nebulas[idx].q2-1)*10+nebulas[idx].y; tz = (nebulas[idx].q3-1)*10+nebulas[idx].z;
                found = true;
            }
        }
        /* 10. Pulsars (Local) */
        else if (tid >= 9000 && tid < 9000+MAX_PULSARS) {
            int idx = tid - 9000;
            if (pulsars[idx].active && pulsars[idx].q1==pq1 && pulsars[idx].q2==pq2 && pulsars[idx].q3==pq3) {
                tx = (pulsars[idx].q1-1)*10+pulsars[idx].x; ty = (pulsars[idx].q2-1)*10+pulsars[idx].y; tz = (pulsars[idx].q3-1)*10+pulsars[idx].z;
                found = true;
            }
        }
        /* 11. Derelicts (Local) */
        else if (tid >= 11000 && tid < 11000+MAX_DERELICTS) {
            int idx = tid - 11000;
            if (derelicts[idx].active && derelicts[idx].q1==pq1 && derelicts[idx].q2==pq2 && derelicts[idx].q3==pq3) {
                tx = (derelicts[idx].q1-1)*10+derelicts[idx].x; ty = (derelicts[idx].q2-1)*10+derelicts[idx].y; tz = (derelicts[idx].q3-1)*10+derelicts[idx].z;
                found = true;
            }
        }
        /* 12. Asteroids (Local) */
        else if (tid >= 12000 && tid < 12000+MAX_ASTEROIDS) {
            int idx = tid - 12000;
            if (asteroids[idx].active && asteroids[idx].q1==pq1 && asteroids[idx].q2==pq2 && asteroids[idx].q3==pq3) {
                tx = (asteroids[idx].q1-1)*10+asteroids[idx].x; ty = (asteroids[idx].q2-1)*10+asteroids[idx].y; tz = (asteroids[idx].q3-1)*10+asteroids[idx].z;
                found = true;
            }
        }
        /* 13. Mines (Local) */
        else if (tid >= 14000 && tid < 14000+MAX_MINES) {
            int idx = tid - 14000;
            if (mines[idx].active && mines[idx].q1==pq1 && mines[idx].q2==pq2 && mines[idx].q3==pq3) {
                tx = (mines[idx].q1-1)*10+mines[idx].x; ty = (mines[idx].q2-1)*10+mines[idx].y; tz = (mines[idx].q3-1)*10+mines[idx].z;
                found = true;
            }
        }
        /* 14. Buoys (Local) */
        else if (tid >= 15000 && tid < 15000+MAX_BUOYS) {
            int idx = tid - 15000;
            if (buoys[idx].active && buoys[idx].q1==pq1 && buoys[idx].q2==pq2 && buoys[idx].q3==pq3) {
                tx = (buoys[idx].q1-1)*10+buoys[idx].x; ty = (buoys[idx].q2-1)*10+buoys[idx].y; tz = (buoys[idx].q3-1)*10+buoys[idx].z;
                found = true;
            }
        }
        /* 15. Platforms (Local) */
        else if (tid >= 16000 && tid < 16000+MAX_PLATFORMS) {
            int idx = tid - 16000;
            if (platforms[idx].active && platforms[idx].q1==pq1 && platforms[idx].q2==pq2 && platforms[idx].q3==pq3) {
                tx = (platforms[idx].q1-1)*10+platforms[idx].x; ty = (platforms[idx].q2-1)*10+platforms[idx].y; tz = (platforms[idx].q3-1)*10+platforms[idx].z;
                found = true;
            }
        }
        /* 16. Rifts (Local) */
        else if (tid >= 17000 && tid < 17000+MAX_RIFTS) {
            int idx = tid - 17000;
            if (rifts[idx].active && rifts[idx].q1==pq1 && rifts[idx].q2==pq2 && rifts[idx].q3==pq3) {
                tx = (rifts[idx].q1-1)*10+rifts[idx].x; ty = (rifts[idx].q2-1)*10+rifts[idx].y; tz = (rifts[idx].q3-1)*10+rifts[idx].z;
                found = true;
            }
        }

        if(found) {
            double cx = players[i].gx; double cy = players[i].gy; double cz = players[i].gz;
            double dx = tx - cx, dy = ty - cy, dz = tz - cz; double d = sqrt(dx*dx + dy*dy + dz*dz);
            if(d > tdist) {
                double h = atan2(dx, -dy) * 180.0 / M_PI; if(h < 0) h += 360; 
                double m = asin(dz/d) * 180.0 / M_PI;
                players[i].target_h = h; players[i].target_m = m; players[i].dx = dx/d; players[i].dy = dy/d; players[i].dz = dz/d;
                players[i].target_gx = cx + players[i].dx * (d - tdist); 
                players[i].target_gy = cy + players[i].dy * (d - tdist); 
                players[i].target_gz = cz + players[i].dz * (d - tdist);
                players[i].nav_state = NAV_STATE_ALIGN; players[i].nav_timer = 60; 
                players[i].start_h = players[i].state.ent_h; players[i].start_m = players[i].state.ent_m;
                send_server_msg(i, "HELMSMAN", "Autopilot engaged. Approaching target.");
            } else send_server_msg(i, "COMPUTER", "Target already in range.");
        } else send_server_msg(i, "COMPUTER", "Target not identified or out of sensor range.");
    } else {
        send_server_msg(i, "COMPUTER", "Usage: apr <ID> <DIST>");
    }
}

void handle_cha(int i, const char *params) {
    if (players[i].state.lock_target > 0) {
        players[i].nav_state = NAV_STATE_CHASE;
        send_server_msg(i, "HELMSMAN", "Chase mode engaged. Intercepting target vector.");
    } else {
        send_server_msg(i, "COMPUTER", "Unable to comply. No target locked.");
    }
}

void handle_srs(int i, const char *params) {
    char b[65536]; /* Massive buffer for very high-density quadrants */
    int q1=players[i].state.q1, q2=players[i].state.q2, q3=players[i].state.q3; 
    double s1=players[i].state.s1, s2=players[i].state.s2, s3=players[i].state.s3;

    snprintf(b, sizeof(b), CYAN "\n--- SHORT RANGE SENSOR ANALYSIS ---" RESET "\nQUADRANT: [%d,%d,%d] | SECTOR: [%.1f,%.1f,%.1f]\n", q1, q2, q3, s1, s2, s3);
    snprintf(b+strlen(b), sizeof(b)-strlen(b), "ENERGY: %d | TORPEDOES: %d | STATUS: %s\n", players[i].state.energy, players[i].state.torpedoes, players[i].state.is_cloaked ? MAGENTA "CLOAKED" RESET : GREEN "NORMAL" RESET);
    strncat(b, "\nTYPE       ID    POSITION      DIST   H / M         DETAILS\n", sizeof(b)-strlen(b)-1);

    QuadrantIndex *local_q = &spatial_index[q1][q2][q3];
    int locked_id = players[i].state.lock_target;
    bool chasing = (players[i].nav_state == NAV_STATE_CHASE);

    /* 1. Players */
    for(int j=0; j<local_q->player_count; j++) {
        ConnectedPlayer *p = local_q->players[j]; if (p == &players[i] || p->state.is_cloaked) continue;
        double dx=p->state.s1-s1, dy=p->state.s2-s2, dz=p->state.s3-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=asin(dz/d)*180/M_PI;
        int pid = (int)(p-players)+1;
        char status[64] = "";
        if (pid == locked_id) { strcat(status, RED "[LOCKED]" RESET); if(chasing) strcat(status, B_RED "[CHASE]" RESET); }
        SAFE_APPEND(b, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     %s (Player) [E:%d] %s\n", "Vessel", pid, p->state.s1, p->state.s2, p->state.s3, d, h, m, p->name, p->state.energy, status);
    }

    /* 2. NPC Ships */
    for(int n=0; n<local_q->npc_count; n++) {
        NPCShip *npc = local_q->npcs[n];
        double dx=npc->x-s1, dy=npc->y-s2, dz=npc->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=asin(dz/d)*180/M_PI;
        int nid = npc->id+1000;
        char status[64] = "";
        if (nid == locked_id) { strcat(status, RED "[LOCKED]" RESET); if(chasing) strcat(status, B_RED "[CHASE]" RESET); }
        SAFE_APPEND(b, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     %s [E:%d] [Engines:%.0f%%] %s\n", "Vessel", nid, npc->x, npc->y, npc->z, d, h, m, get_species_name(npc->faction), npc->energy, npc->engine_health, status);
    }

    /* 3. Starbases */
    for(int b_idx=0; b_idx<local_q->base_count; b_idx++) {
        NPCBase *ba = local_q->bases[b_idx];
        double dx=ba->x-s1, dy=ba->y-s2, dz=ba->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int baid = ba->id+2000;
        char status[64] = "";
        if (baid == locked_id) { strcat(status, RED "[LOCKED]" RESET); if(chasing) strcat(status, B_RED "[CHASE]" RESET); }
        SAFE_APPEND(b, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Federation Starbase %s\n", "Starbase", baid, ba->x, ba->y, ba->z, d, h, m, status);
    }

    /* 4. Planets */
    for(int p_idx=0; p_idx<local_q->planet_count; p_idx++) {
        NPCPlanet *pl = local_q->planets[p_idx];
        double dx=pl->x-s1, dy=pl->y-s2, dz=pl->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int plid = pl->id+3000;
        char status[64] = ""; if (plid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Class-M Planet %s\n", "Planet", plid, pl->x, pl->y, pl->z, d, h, m, status);
    }

    /* 5. Stars */
    for(int s_idx=0; s_idx<local_q->star_count; s_idx++) {
        NPCStar *st = local_q->stars[s_idx];
        double dx=st->x-s1, dy=st->y-s2, dz=st->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int sid = st->id+4000;
        char status[64] = ""; if (sid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Star %s\n", "Star", sid, st->x, st->y, st->z, d, h, m, status);
    }

    /* 6. Black Holes */
    for(int h_idx=0; h_idx<local_q->bh_count; h_idx++) {
        NPCBlackHole *bh = local_q->black_holes[h_idx];
        double dx=bh->x-s1, dy=bh->y-s2, dz=bh->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double hh=atan2(dx,-dy)*180/M_PI; if(hh<0)hh+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int bid = bh->id+7000;
        char status[64] = ""; if (bid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Black Hole (Grav Pull) %s\n", "B-Hole", bid, bh->x, bh->y, bh->z, d, hh, m, status);
    }

    /* 7. Nebulas */
    for(int n_idx=0; n_idx<local_q->nebula_count; n_idx++) {
        NPCNebula *nb = local_q->nebulas[n_idx];
        double dx=nb->x-s1, dy=nb->y-s2, dz=nb->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int nid = nb->id+8000;
        char status[64] = ""; if (nid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Mutara Nebula %s\n", "Nebula", nid, nb->x, nb->y, nb->z, d, h, m, status);
    }

    /* 8. Pulsars */
    for (int p_idx=0; p_idx<local_q->pulsar_count; p_idx++) {
        NPCPulsar *pu = local_q->pulsars[p_idx];
        double dx=pu->x-s1, dy=pu->y-s2, dz=pu->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int puid = pu->id+9000;
        char status[64] = ""; if (puid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Pulsar (Radiation) %s\n", "Pulsar", puid, pu->x, pu->y, pu->z, d, h, m, status);
    }

    /* 9. Comets */
    for (int c_idx=0; c_idx<local_q->comet_count; c_idx++) {
        NPCComet *co = local_q->comets[c_idx];
        double dx=co->x-s1, dy=co->y-s2, dz=co->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int cid = co->id+10000;
        char status[64] = ""; if (cid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Comet (Energy Source) %s\n", "Comet", cid, co->x, co->y, co->z, d, h, m, status);
    }

    /* 10. Asteroids */
    for (int a_idx = 0; a_idx < local_q->asteroid_count; a_idx++) {
        NPCAsteroid *as = local_q->asteroids[a_idx];
        double dx=as->x-s1, dy=as->y-s2, dz=as->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int aid = as->id+12000;
        char status[64] = ""; if (aid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Asteroid (Hazard) %s\n", "Asteroid", aid, as->x, as->y, as->z, d, h, m, status);
    }

    /* 11. Monsters */
    for (int m_idx = 0; m_idx < local_q->monster_count; m_idx++) {
        NPCMonster *mo = local_q->monsters[m_idx];
        double dx=mo->x-s1, dy=mo->y-s2, dz=mo->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int moid = mo->id+18000;
        char status[64] = ""; if (moid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     %s %s\n", "Monster", moid, mo->x, mo->y, mo->z, d, h, m, (mo->type==30)?"Crystalline Entity":"Space Amoeba", status);
    }

    /* 12. Derelicts */
    for (int d_idx = 0; d_idx < local_q->derelict_count; d_idx++) {
        NPCDerelict *dr = local_q->derelicts[d_idx];
        double dx=dr->x-s1, dy=dr->y-s2, dz=dr->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int drid = dr->id+11000;
        char status[64] = ""; if (drid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Derelict Ship %s\n", "Derelict", drid, dr->x, dr->y, dr->z, d, h, m, status);
    }

    /* 13. Defense Platforms */
    for (int pt_idx = 0; pt_idx < local_q->platform_count; pt_idx++) {
        NPCPlatform *pt = local_q->platforms[pt_idx];
        double dx=pt->x-s1, dy=pt->y-s2, dz=pt->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int ptid = pt->id+16000;
        char status[64] = ""; if (ptid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Defense Platform %s\n", "Platform", ptid, pt->x, pt->y, pt->z, d, h, m, status);
    }

    /* 14. Rifts, Buoys, Mines */
    for (int rf_idx = 0; rf_idx < local_q->rift_count; rf_idx++) {
        NPCRift *rf = local_q->rifts[rf_idx];
        double dx=rf->x-s1, dy=rf->y-s2, dz=rf->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int rfid = rf->id+17000;
        char status[64] = ""; if (rfid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Spatial Rift %s\n", "Rift", rfid, rf->x, rf->y, rf->z, d, h, m, status);
    }
    for (int bu_idx = 0; bu_idx < local_q->buoy_count; bu_idx++) {
        NPCBuoy *bu = local_q->buoys[bu_idx];
        double dx=bu->x-s1, dy=bu->y-s2, dz=bu->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m_val=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int buid = bu->id+15000;
        char status[64] = ""; if (buid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Comm Buoy %s\n", "Buoy", buid, bu->x, bu->y, bu->z, d, h, m_val, status);
    }
    for (int mi_idx = 0; mi_idx < local_q->mine_count; mi_idx++) {
        NPCMine *mi = local_q->mines[mi_idx];
        double dx=mi->x-s1, dy=mi->y-s2, dz=mi->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m_val=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int miid = mi->id+14000;
        char status[64] = ""; if (miid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Cloaked Mine %s\n", "Mine", miid, mi->x, mi->y, mi->z, d, h, m_val, status);
    }

    /* 15. Neighborhood Scan (Inter-Quadrant awareness) */
    bool found_neighbor = false;
    char neighbor_buf[4096] = "";
    if (s1 < 2.5 || s1 > 7.5 || s2 < 2.5 || s2 > 7.5 || s3 < 2.5 || s3 > 7.5) {
        for (int dq1 = -1; dq1 <= 1; dq1++) {
            for (int dq2 = -1; dq2 <= 1; dq2++) {
                for (int dq3 = -1; dq3 <= 1; dq3++) {
                    if (dq1 == 0 && dq2 == 0 && dq3 == 0) continue;
                    int nq1 = q1 + dq1, nq2 = q2 + dq2, nq3 = q3 + dq3;
                    if (!IS_Q_VALID(nq1, nq2, nq3)) continue;
                    QuadrantIndex *nq = &spatial_index[nq1][nq2][nq3];
                    double off_x = dq1 * 10.0, off_y = dq2 * 10.0, off_z = dq3 * 10.0;
                    
                    /* Scan for mobile entities and beacons only */
                    for(int n=0; n<nq->npc_count; n++) {
                        NPCShip *npc = nq->npcs[n]; if (!npc->active) continue;
                        double dx=(npc->x+off_x)-s1, dy=(npc->y+off_y)-s2, dz=(npc->z+off_z)-s3;
                        double d=sqrt(dx*dx+dy*dy+dz*dz); if (d > 8.0) continue;
                        double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=asin(dz/d)*180/M_PI;
                        if(!found_neighbor) { sprintf(neighbor_buf, YELLOW "\n--- NEIGHBORHOOD SENSOR SCAN (Adjacent Quadrants) ---" RESET "\nTYPE       ID    QUADRANT      DIST   H / M         DETAILS\n"); found_neighbor=true; }
                        char line[256]; sprintf(line, "%-10s %-5d [%d,%d,%d] %-5.1f %03.0f / %+03.0f     %s (NPC)\n", "Vessel", npc->id+1000, nq1, nq2, nq3, d, h, m, get_species_name(npc->faction));
                        strcat(neighbor_buf, line);
                    }
                    for(int c=0; c<nq->comet_count; c++) {
                        NPCComet *co = nq->comets[c]; if (!co->active) continue;
                        double dx=(co->x+off_x)-s1, dy=(co->y+off_y)-s2, dz=(co->z+off_z)-s3;
                        double d=sqrt(dx*dx+dy*dy+dz*dz); if (d > 8.0) continue;
                        double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=asin(dz/d)*180/M_PI;
                        if(!found_neighbor) { sprintf(neighbor_buf, YELLOW "\n--- NEIGHBORHOOD SENSOR SCAN (Adjacent Quadrants) ---" RESET "\nTYPE       ID    QUADRANT      DIST   H / M         DETAILS\n"); found_neighbor=true; }
                        char line[256]; sprintf(line, "%-10s %-5d [%d,%d,%d] %-5.1f %03.0f / %+03.0f     Comet\n", "Comet", co->id+10000, nq1, nq2, nq3, d, h, m);
                        strcat(neighbor_buf, line);
                    }
                    for(int bu=0; bu<nq->buoy_count; bu++) {
                        NPCBuoy *buoy = nq->buoys[bu]; if (!buoy->active) continue;
                        double dx=(buoy->x+off_x)-s1, dy=(buoy->y+off_y)-s2, dz=(buoy->z+off_z)-s3;
                        double d=sqrt(dx*dx+dy*dy+dz*dz); if (d > 8.0) continue;
                        double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=asin(dz/d)*180/M_PI;
                        if(!found_neighbor) { sprintf(neighbor_buf, YELLOW "\n--- NEIGHBORHOOD SENSOR SCAN (Adjacent Quadrants) ---" RESET "\nTYPE       ID    QUADRANT      DIST   H / M         DETAILS\n"); found_neighbor=true; }
                        char line[256]; sprintf(line, "%-10s %-5d [%d,%d,%d] %-5.1f %03.0f / %+03.0f     Comm Buoy\n", "Buoy", buoy->id+15000, nq1, nq2, nq3, d, h, m);
                        strcat(neighbor_buf, line);
                    }
                }
            }
        }
    }
    if(found_neighbor) strcat(b, neighbor_buf);

    #undef SAFE_APPEND
    send_server_msg(i, "TACTICAL", b);
}

void handle_lrs(int i, const char *params) {
    char b[16384]; 
    int q1=players[i].state.q1, q2=players[i].state.q2, q3=players[i].state.q3;
    double s1=players[i].state.s1, s2=players[i].state.s2, s3=players[i].state.s3;
    
    snprintf(b, sizeof(b), B_CYAN "\n.--- LCARS LONG RANGE TACTICAL SENSORS --------------------------------------.\n" RESET);
    char status[256];
    sprintf(status, WHITE " POS: [%d,%d,%d] SECTOR: [%.1f,%.1f,%.1f] | HDG: %03.0f MRK: %+03.0f\n" RESET, 
            q1, q2, q3, s1, s2, s3, players[i].state.ent_h, players[i].state.ent_m);
    strcat(b, status);
    strcat(b, B_CYAN "'------------------------------------------------------------------------------'\n" RESET);
    strcat(b, " DATA: [ H:B-Hole P:Planet N:NPC B:Base S:Star ]\n");
    strcat(b, " Symbols: ~:Nebula *:Pulsar +:Comet #:Asteroid M:Monster >:Rift\n\n");

    const char* section_names[] = {"[ GREEN TACTICAL ZONE ]", "[ YELLOW TACTICAL ZONE ]", "[ RED TACTICAL ZONE ]"};
    const char* section_colors[] = {B_GREEN, B_YELLOW, B_RED};

    for (int section = 0; section < 3; section++) {
        int dq3 = (section == 0) ? 1 : (section == 1) ? 0 : -1;
        int nq3 = q3 + dq3;
        
        if (nq3 < 1 || nq3 > 10) continue;

        char header[128];
        sprintf(header, "%s%s (Level Z:%d)" RESET "\n", section_colors[section], section_names[section], nq3);
        strcat(b, header);
        
        for (int dq2 = -1; dq2 <= 1; dq2++) {
            int nq2 = q2 + dq2;
            char line1[2048] = "  ";
            char line2[2048] = "  ";
            
            for (int dq1 = -1; dq1 <= 1; dq1++) {
                int nq1 = q1 + dq1;
                char cell1[256], cell2[256];
                
                if (IS_Q_VALID(nq1, nq2, nq3)) {
                    long long v = galaxy_master.g[nq1][nq2][nq3];
                    int s=v%10, b_cnt=(v/10)%10, k=(v/100)%10, p=(v/1000)%10, bh=(v/10000)%10;
                    int neb=(v/100000)%10, pul=(v/1000000)%10, com=(v/100000000)%10, ast=(v/1000000000)%10;
                    int mon=(v/10000000000000000LL)%10;
                    int rift=(v/100000000000000LL)%10;
                    
                    /* Navigation Solution */
                    double dx = (nq1 - q1) * 10.0; double dy = (nq2 - q2) * 10.0; double dz = (nq3 - q3) * 10.0;
                    double dist = sqrt(dx*dx + dy*dy + dz*dz);
                    double h_v = 0, m_v = 0;
                    if (dist > 0.01) { h_v = atan2(dx, -dy) * 180.0 / M_PI; if(h_v < 0) h_v += 360; m_v = asin(dz / dist) * 180.0 / M_PI; }

                    /* LINE 1: COORDS & NAV */
                    if (nq1==q1 && nq2==q2 && nq3==q3)
                        sprintf(cell1, B_BLUE "[ %d,%d,%d ]" RESET "  *- CURRENT -*   ", nq1, nq2, nq3);
                    else
                        sprintf(cell1, WHITE "[ %d,%d,%d ]" RESET "  %03.0f/%+03.0f/W%.1f  ", nq1, nq2, nq3, h_v, m_v, dist/10.0);

                    /* LINE 2: OBJECTS [ H P N B S ] */
                    char h_s[32], p_s[32], n_s[32], b_s[32], s_s[32];
                    if(bh>0) sprintf(h_s, "%s%d" RESET, MAGENTA, bh); else strcpy(h_s, ".");
                    if(p>0) sprintf(p_s, "%s%d" RESET, CYAN, p); else strcpy(p_s, ".");
                    if(k>0) sprintf(n_s, "%s%d" RESET, RED, k); else strcpy(n_s, ".");
                    if(b_cnt>0) sprintf(b_s, "%s%d" RESET, GREEN, b_cnt); else strcpy(b_s, ".");
                    if(s>0) sprintf(s_s, "%s%d" RESET, YELLOW, s); else strcpy(s_s, ".");

                    char an[16] = "";
                    if(neb>0) { strcat(an, "~"); } if(pul>0) { strcat(an, "*"); } if(com>0) { strcat(an, "+"); }
                    if(ast>0) { strcat(an, "#"); } if(mon>0) { strcat(an, "M"); } if(rift>0) { strcat(an, ">"); }
                    
                    sprintf(cell2, "  [%s %s %s %s %s" RESET "] %-5s      ", h_s, p_s, n_s, b_s, s_s, an);
                } else {
                    strcpy(cell1, "  [ -,-,- ]  -------------   ");
                    strcpy(cell2, "  [ . . . . . ]              ");
                }
                strcat(line1, cell1); strcat(line2, cell2);
            }
            strcat(b, line1); strcat(b, "\n");
            strcat(b, line2); strcat(b, "\n\n");
        }
    }
    strcat(b, B_CYAN "'------------------------------------------------------------------------------'\n" RESET);
    send_server_msg(i, "SCIENCE", b);
}

void handle_pha(int i, const char *params) {
    int e, tid; 
    int args = sscanf(params, " %d %d", &tid, &e);
    
    if (args == 1) {
        /* User provided only energy, use current lock target */
        e = tid;
        tid = players[i].state.lock_target;
        if (tid == 0) {
            send_server_msg(i, "COMPUTER", "No target locked. Usage: pha <ID> <E> or lock a target.");
            return;
        }
    } else if (args != 2) {
        send_server_msg(i, "COMPUTER", "Usage: pha <ID> <E> or pha <E> (requires lock)");
        return;
    }

    if (players[i].state.energy < e) { send_server_msg(i, "COMPUTER", "Insufficient energy for phaser burst."); return; }
    players[i].state.energy -= e;
    
    double tx, ty, tz; bool found = false;
    int pq1=players[i].state.q1, pq2=players[i].state.q2, pq3=players[i].state.q3;
    
    if (tid >= 1 && tid <= 32 && players[tid-1].active && players[tid-1].state.q1 == pq1 && players[tid-1].state.q2 == pq2 && players[tid-1].state.q3 == pq3) { tx=players[tid-1].state.s1; ty=players[tid-1].state.s2; tz=players[tid-1].state.s3; found=true; }
    else if (tid >= 1000 && tid < 1000+MAX_NPC && npcs[tid-1000].active && npcs[tid-1000].q1 == pq1 && npcs[tid-1000].q2 == pq2 && npcs[tid-1000].q3 == pq3) { tx=npcs[tid-1000].x; ty=npcs[tid-1000].y; tz=npcs[tid-1000].z; found=true; }
    else if (tid >= 16000 && tid < 16000+MAX_PLATFORMS && platforms[tid-16000].active && platforms[tid-16000].q1 == pq1 && platforms[tid-16000].q2 == pq2 && platforms[tid-16000].q3 == pq3) { tx=platforms[tid-16000].x; ty=platforms[tid-16000].y; tz=platforms[tid-16000].z; found=true; }
    else if (tid >= 18000 && tid < 18000+MAX_MONSTERS && monsters[tid-18000].active && monsters[tid-18000].q1 == pq1 && monsters[tid-18000].q2 == pq2 && monsters[tid-18000].q3 == pq3) { tx=monsters[tid-18000].x; ty=monsters[tid-18000].y; tz=monsters[tid-18000].z; found=true; }
    
    if (found) {
        double dx=tx-players[i].state.s1, dy=ty-players[i].state.s2, dz=tz-players[i].state.s3; double dist=sqrt(dx*dx+dy*dy+dz*dz);
        if (dist < 0.1) dist = 0.1;
        int hit = (int)((e / dist) * (players[i].state.system_health[4] / 100.0f));
        players[i].state.beam_count = 1; players[i].state.beams[0] = (NetBeam){(float)tx, (float)ty, (float)tz, 1};
        if (tid <= 32) {
            int dmg_rem = hit;
            for(int s=0; s<6; s++) { int abs=(players[tid-1].state.shields[s]>=dmg_rem/6)?dmg_rem/6:players[tid-1].state.shields[s]; players[tid-1].state.shields[s]-=abs; dmg_rem-=abs; }
            players[tid-1].state.energy -= dmg_rem; if (players[tid-1].state.energy <= 0) { players[tid-1].state.energy = 0; players[tid-1].state.crew_count = 0; players[tid-1].active = 0; players[tid-1].state.boom = (NetPoint){(float)tx,(float)ty,(float)tz,1}; }
            send_server_msg(tid-1, "WARNING", "UNDER PHASER ATTACK!");
        } else if (tid >= 1000 && tid < 1000+MAX_NPC) {
            npcs[tid-1000].energy -= hit; float engine_dmg = (hit / 1000.0f) * 10.0f; npcs[tid-1000].engine_health -= engine_dmg; if (npcs[tid-1000].engine_health < 0) npcs[tid-1000].engine_health = 0;
            if (npcs[tid-1000].energy <= 0) { npcs[tid-1000].active = 0; players[i].state.boom = (NetPoint){(float)npcs[tid-1000].x, (float)npcs[tid-1000].y, (float)npcs[tid-1000].z, 1}; }
        } else if (tid >= 16000 && tid < 16000+MAX_PLATFORMS) {
            platforms[tid-16000].energy -= hit;
            if (platforms[tid-16000].energy <= 0) { platforms[tid-16000].active = 0; players[i].state.boom = (NetPoint){(float)platforms[tid-16000].x, (float)platforms[tid-16000].y, (float)platforms[tid-16000].z, 1}; }
        } else if (tid >= 18000 && tid < 18000+MAX_MONSTERS) {
            monsters[tid-18000].energy -= hit;
            if (monsters[tid-18000].energy <= 0) { monsters[tid-18000].active = 0; players[i].state.boom = (NetPoint){(float)monsters[tid-18000].x, (float)monsters[tid-18000].y, (float)monsters[tid-18000].z, 1}; }
        }
        char msg[64]; sprintf(msg, "Phasers locked. Target hit for %d damage.", hit); send_server_msg(i, "TACTICAL", msg);
    } else send_server_msg(i, "COMPUTER", "Target out of phaser range or not in current quadrant.");
}

void handle_tor(int i, const char *params) {
    if(players[i].state.torpedoes > 0) {
        players[i].state.torpedoes--; players[i].torp_active=true;
        players[i].torp_target = players[i].state.lock_target;
        double h=players[i].state.ent_h, m=players[i].state.ent_m;
        bool manual = true; if (players[i].torp_target > 0) manual = false;
        double rad_h = h * M_PI / 180.0; double rad_m = m * M_PI / 180.0;
        players[i].tx = players[i].state.s1; players[i].ty = players[i].state.s2; players[i].tz = players[i].state.s3;
        players[i].tdx = cos(rad_m) * sin(rad_h); players[i].tdy = cos(rad_m) * -cos(rad_h); players[i].tdz = sin(rad_m);
        send_server_msg(i, "TACTICAL", manual ? "Torpedo away (Manual)." : "Torpedo away (Locked).");
    } else send_server_msg(i, "TACTICAL", "Insufficient torpedoes.");
}

void handle_she(int i, const char *params) {
    int f,r,t,b,l,ri; if(sscanf(params, "%d %d %d %d %d %d", &f,&r,&t,&b,&l,&ri) == 6) {
        players[i].state.shields[0]=f; players[i].state.shields[1]=r; players[i].state.shields[2]=t;
        players[i].state.shields[3]=b; players[i].state.shields[4]=l; players[i].state.shields[5]=ri;
        send_server_msg(i, "ENGINEERING", "Shields configured.");
    } else send_server_msg(i, "COMPUTER", "Usage: she <F> <R> <T> <B> <L> <RI>");
}

void handle_lock(int i, const char *params) {
    int tid; if(sscanf(params, " %d", &tid) == 1) {
        players[i].state.lock_target = tid; send_server_msg(i, "TACTICAL", "Target locked.");
    } else { players[i].state.lock_target = 0; send_server_msg(i, "TACTICAL", "Lock released."); }
}

void handle_scan(int i, const char *params) {
    int tid; if(sscanf(params, " %d", &tid) == 1) {
        char rep[1024]; bool found = false;
        int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
        if (tid >= 1 && tid <= 32) {
             ConnectedPlayer *t = &players[tid-1];
             if (t->active && t->state.q1 == pq1 && t->state.q2 == pq2 && t->state.q3 == pq3) {
                 found = true;
                 snprintf(rep, sizeof(rep), CYAN "\n--- SENSOR SCAN ANALYSIS: TARGET ID %d ---" RESET, tid);
                 snprintf(rep+strlen(rep), sizeof(rep)-strlen(rep), "COMMANDER: %s\n", t->name);
                 snprintf(rep+strlen(rep), sizeof(rep)-strlen(rep), "ENERGY: %d | CREW: %d | TORPS: %d\n", t->state.energy, t->state.crew_count, t->state.torpedoes);
                 strcat(rep, BLUE "SYSTEMS STATUS:\n" RESET);
                 const char* sys[] = {"Warp", "Impulse", "Sensors", "Transp", "Phasers", "Torps", "Computer", "Life"};
                 for(int s=0; s<8; s++) {
                     char bar[11]; int fills = (int)(t->state.system_health[s]/10.0f); for(int k=0; k<10; k++) bar[k]=(k<fills)?'|':'.'; bar[10]=0;
                     char line[64]; snprintf(line, sizeof(line), " %-8s [%s] %.1f%%\n", sys[s], bar, t->state.system_health[s]);
                     strcat(rep, line);
                 }
             }
        } 
        if (!found && tid >= 1000 && tid < 1000+MAX_NPC) {
            int idx = tid - 1000;
            if (npcs[idx].active && npcs[idx].q1==pq1 && npcs[idx].q2==pq2 && npcs[idx].q3==pq3) {
                found = true;
                NPCShip *n = &npcs[idx];
                snprintf(rep, sizeof(rep), CYAN "\n--- TACTICAL SCAN ANALYSIS: TARGET ID %d ---" RESET, tid);
                snprintf(rep+strlen(rep), sizeof(rep)-strlen(rep), "SPECIES: %s\n", get_species_name(n->faction));
                snprintf(rep+strlen(rep), sizeof(rep)-strlen(rep), "ENERGY CORE: %d\n", n->energy);
                snprintf(rep+strlen(rep), sizeof(rep)-strlen(rep), "PROPULSION: %.1f%%\n", n->engine_health);
                snprintf(rep+strlen(rep), sizeof(rep)-strlen(rep), "BEHAVIOR: %s\n", (n->ai_state==AI_STATE_FLEE)?"RETREATING":(n->ai_state==AI_STATE_CHASE)?"AGGRESSIVE":"PATROLLING");
            }
        } 
        if (!found && tid >= 2000 && tid < 2000+MAX_BASES) {
            int idx = tid - 2000;
            if (bases[idx].active && bases[idx].q1==pq1 && bases[idx].q2==pq2 && bases[idx].q3==pq3) {
                found = true;
                snprintf(rep, sizeof(rep), WHITE "\n--- FEDERATION STARBASE ANALYSIS ---" RESET "\nTYPE: Supply and Repair Outpost\nSTATUS: Active\nSERVICES: Full Repair, Torpedo Reload, Energy Recharge.\n");
            }
        } 
        if (!found && tid >= 3000 && tid < 3000+MAX_PLANETS) {
            int idx = tid - 3000;
            if (planets[idx].active && planets[idx].q1==pq1 && planets[idx].q2==pq2 && planets[idx].q3==pq3) {
                found = true;
                const char* res[] = {"-","Dilithium","Tritanium","Verterium","Monotanium","Isolinear","Gases"};
                snprintf(rep, sizeof(rep), GREEN "\n--- PLANETARY SURVEY ---" RESET "\nTYPE: Class-M Habitable\nRESOURCE: %s\nRESERVES: %d units\n", res[planets[idx].resource_type], planets[idx].amount);
            }
        } 
        if (!found && tid >= 4000 && tid < 4000+MAX_STARS) {
            int idx = tid - 4000;
            if (stars_data[idx].active && stars_data[idx].q1==pq1 && stars_data[idx].q2==pq2 && stars_data[idx].q3==pq3) {
                found = true;
                snprintf(rep, sizeof(rep), YELLOW "\n--- STELLAR ANALYSIS ---" RESET "\nTYPE: Main Sequence G-Class Star\nLUMINOSITY: Standard\nADVISORY: Proximity scooping active (sco).\n");
            }
        } 
        if (!found && tid >= 7000 && tid < 7000+MAX_BH) {
            int idx = tid - 7000;
            if (black_holes[idx].active && black_holes[idx].q1==pq1 && black_holes[idx].q2==pq2 && black_holes[idx].q3==pq3) {
                found = true;
                snprintf(rep, sizeof(rep), MAGENTA "\n--- SINGULARITY ANALYSIS ---" RESET "\nTYPE: Schwarzschild Black Hole\nEFFECT: Extreme Time-Dilation and Space Curvature.\nADVISORY: Significant gravitational pull detected within 3.0 units. Escape velocity required.\n");
            }
        } 
        if (!found && tid >= 8000 && tid < 8000+MAX_NEBULAS) {
            int idx = tid - 8000;
            if (nebulas[idx].active && nebulas[idx].q1==pq1 && nebulas[idx].q2==pq2 && nebulas[idx].q3==pq3) {
                found = true;
                snprintf(rep, sizeof(rep), BLUE "\n--- STELLAR PHENOMENON ANALYSIS ---" RESET "\nTYPE: Class-Mutara Nebula\nCOMPOSITION: Ionized Gases, Sensor-dampening particulates.\nEFFECT: Reduced sensor range, Shield regeneration inhibition.\n");
            }
        } 
        if (!found && tid >= 9000 && tid < 9000+MAX_PULSARS) {
            int idx = tid - 9000;
            if (pulsars[idx].active && pulsars[idx].q1==pq1 && pulsars[idx].q2==pq2 && pulsars[idx].q3==pq3) {
                found = true;
                snprintf(rep, sizeof(rep), RED "\n--- WARNING: PULSAR DETECTED ---" RESET "\nTYPE: Rotating Neutron Star\nRADIATION: Extreme (Gamma/X-Ray)\nADVISORY: Maintain minimum safe distance 2.0. Shield failure imminent in proximity.\n");
            }
        } 
        if (!found && tid >= 10000 && tid < 10000+MAX_COMETS) {
            int idx = tid - 10000;
            if (comets[idx].active && comets[idx].q1==pq1 && comets[idx].q2==pq2 && comets[idx].q3==pq3) {
                found = true;
                snprintf(rep, sizeof(rep), CYAN "\n--- COMET TRACKING DATA ---" RESET "\nTYPE: Icy Nucleus / Ion Tail\nSPEED: Orbital Intercept possible.\nCOMPOSITION: Rare gases, frozen verterium.\n");
            }
        } 
        if (!found && tid >= 11000 && tid < 11000+MAX_DERELICTS) {
            int idx = tid - 11000;
            if (derelicts[idx].active && derelicts[idx].q1==pq1 && derelicts[idx].q2==pq2 && derelicts[idx].q3==pq3) {
                found = true;
                snprintf(rep, sizeof(rep), WHITE "\n--- DERELICT SENSOR LOG ---" RESET "\nTYPE: Abandoned Vessel\nINTEGRITY: Critical (Adrift)\nADVISORY: Boarding (bor) may recover valuable resources or tech.\n");
            }
        } 
        if (!found && tid >= 12000 && tid < 12000+MAX_ASTEROIDS) {
            int idx = tid - 12000;
            if (asteroids[idx].active && asteroids[idx].q1==pq1 && asteroids[idx].q2==pq2 && asteroids[idx].q3==pq3) {
                found = true;
                snprintf(rep, sizeof(rep), WHITE "\n--- ASTEROID ANALYSIS ---" RESET "\nTYPE: Carbonaceous / Metallic\nEFFECT: Navigation hazard at high impulse/warp.\n");
            }
        } 
        if (!found && tid >= 16000 && tid < 16000+MAX_PLATFORMS) {
            int idx = tid - 16000;
            if (platforms[idx].active && platforms[idx].q1==pq1 && platforms[idx].q2==pq2 && platforms[idx].q3==pq3) {
                found = true;
                snprintf(rep, sizeof(rep), RED "\n--- DEFENSE PLATFORM TACTICAL ---" RESET "\nTYPE: Automated Weapon Sentry\nSTATUS: ACTIVE / HOSTILE\nENERGY CORE: %d\nCOOLDOWN: %d ticks\n", platforms[idx].energy, platforms[idx].fire_cooldown);
            }
        } 
        if (!found && tid >= 18000 && tid < 18000+MAX_MONSTERS) {
            int idx = tid - 18000;
            if (monsters[idx].active && monsters[idx].q1==pq1 && monsters[idx].q2==pq2 && monsters[idx].q3==pq3) {
                found = true;
                snprintf(rep, sizeof(rep), MAGENTA "\n--- XENO-BIOLOGICAL ANALYSIS ---" RESET "\nTYPE: %s\nTHREAT LEVEL: EXTREME\nADVISORY: Conventional weapons effective but risky in close proximity.\n", (monsters[idx].type==30)?"Crystalline Entity":"Space Amoeba");
            }
        }

        if (found) send_server_msg(i, "SCIENCE", rep);
        else send_server_msg(i, "COMPUTER", "Unable to lock sensors on specified ID.");
    } else {
        send_server_msg(i, "COMPUTER", "Usage: scan <ID>");
    }
}

void handle_clo(int i, const char *params) {
    players[i].state.is_cloaked = !players[i].state.is_cloaked;
    send_server_msg(i, "HELMSMAN", players[i].state.is_cloaked ? "Cloaking device engaged. Sensors limited." : "Cloaking device disengaged.");
}

void handle_bor(int i, const char *params) {
    int tid; if(sscanf(params, " %d", &tid) == 1) {
        if (players[i].state.energy < 5000) { send_server_msg(i, "COMPUTER", "Insufficient energy for boarding operation."); return; }
        players[i].state.energy -= 5000;
        double tx, ty, tz; bool found = false;
        int pq1=players[i].state.q1, pq2=players[i].state.q2, pq3=players[i].state.q3;
        if (tid >= 1 && tid <= 32 && players[tid-1].active && players[tid-1].state.q1 == pq1 && players[tid-1].state.q2 == pq2 && players[tid-1].state.q3 == pq3) { tx=players[tid-1].state.s1; ty=players[tid-1].state.s2; tz=players[tid-1].state.s3; found=true; }
        else if (tid >= 1000 && tid < 1000+MAX_NPC && npcs[tid-1000].active && npcs[tid-1000].q1 == pq1 && npcs[tid-1000].q2 == pq2 && npcs[tid-1000].q3 == pq3) { tx=npcs[tid-1000].x; ty=npcs[tid-1000].y; tz=npcs[tid-1000].z; found=true; }
        else if (tid >= 11000 && tid < 11000+MAX_DERELICTS && derelicts[tid-11000].active && derelicts[tid-11000].q1 == pq1 && derelicts[tid-11000].q2 == pq2 && derelicts[tid-11000].q3 == pq3) { tx=derelicts[tid-11000].x; ty=derelicts[tid-11000].y; tz=derelicts[tid-11000].z; found=true; }
        
        if (found) {
            double dx=tx-players[i].state.s1, dy=ty-players[i].state.s2, dz=tz-players[i].state.s3; double dist=sqrt(dx*dx+dy*dy+dz*dz);
            if (dist < 1.0) {
                if (rand()%100 < 40) { /* 40% success */
                    int reward = rand()%3;
                    if (reward == 0) { players[i].state.inventory[1] += 5; send_server_msg(i, "BOARDING", "Success! Captured Dilithium crystals."); }
                    else if (reward == 1) { players[i].state.inventory[5] += 100; send_server_msg(i, "ENGINEERING", "Salvaged advanced Isolinear Chips from the ship's computer."); }
                    else { for(int s=0; s<8; s++) players[i].state.system_health[s] = 100.0f; send_server_msg(i, "REPAIR", "Found automated repair drones. All systems restored!"); }
                } else if (rand()%100 < 80) {
                    int loss = 5 + rand()%15; players[i].state.crew_count -= loss;
                    send_server_msg(i, "SECURITY", "Boarding party repelled! Heavy casualties reported.");
                } else {
                    send_server_msg(i, "SECURITY", "Operation failed. Enemy systems too heavily defended.");
                }
            } else send_server_msg(i, "COMPUTER", "Target not in transporter range.");
        } else send_server_msg(i, "COMPUTER", "Invalid boarding target.");
    } else send_server_msg(i, "COMPUTER", "Usage: bor <ID>");
}

void handle_dis(int i, const char *params) {
    int tid; if(sscanf(params, " %d", &tid) == 1) {
        int pq1=players[i].state.q1, pq2=players[i].state.q2, pq3=players[i].state.q3;
        if (tid >= 1000 && tid < 1000+MAX_NPC && npcs[tid-1000].active && npcs[tid-1000].q1 == pq1 && npcs[tid-1000].q2 == pq2 && npcs[tid-1000].q3 == pq3) {
            double dx=npcs[tid-1000].x-players[i].state.s1, dy=npcs[tid-1000].y-players[i].state.s2, dz=npcs[tid-1000].z-players[i].state.s3;
            if (sqrt(dx*dx+dy*dy+dz*dz) < 1.5) {
                int yield = (npcs[tid-1000].energy / 100); players[i].state.inventory[2] += yield; players[i].state.inventory[5] += yield / 5; npcs[tid-1000].active = 0;
                players[i].state.dismantle = (NetDismantle){(float)npcs[tid-1000].x, (float)npcs[tid-1000].y, (float)npcs[tid-1000].z, npcs[tid-1000].faction, 1};
                send_server_msg(i, "ENGINEERING", "Vessel dismantled. Resources transferred to cargo bay.");
            } else send_server_msg(i, "COMPUTER", "Not in range for dismantling.");
        } else send_server_msg(i, "COMPUTER", "Invalid dismantle target.");
    }
}

void handle_min(int i, const char *params) {
    bool f=false; 
    /* 1. Cerca Pianeti (Distanza < 2.0) */
    for(int p=0;p<MAX_PLANETS;p++) if(planets[p].active && planets[p].q1==players[i].state.q1 && planets[p].q2==players[i].state.q2 && planets[p].q3==players[i].state.q3) {
        double d=sqrt(pow(planets[p].x-players[i].state.s1,2)+pow(planets[p].y-players[i].state.s2,2)+pow(planets[p].z-players[i].state.s3,2));
        if(d<2.0){ int ex=(planets[p].amount>100)?100:planets[p].amount; planets[p].amount-=ex; players[i].state.inventory[planets[p].resource_type]+=ex; send_server_msg(i,"GEOLOGY","Planetary mining successful."); f=true; break; }
    }
    
    /* 2. Cerca Asteroidi (Distanza < 1.0) if no planet found */
    if(!f) for(int a=0;a<MAX_ASTEROIDS;a++) if(asteroids[a].active && asteroids[a].q1==players[i].state.q1 && asteroids[a].q2==players[i].state.q2 && asteroids[a].q3==players[i].state.q3) {
        double d=sqrt(pow(asteroids[a].x-players[i].state.s1,2)+pow(asteroids[a].y-players[i].state.s2,2)+pow(asteroids[a].z-players[i].state.s3,2));
        if(d<1.0){ 
            int ex=(asteroids[a].amount>50)?50:asteroids[a].amount; 
            asteroids[a].amount-=ex; 
            players[i].state.inventory[asteroids[a].resource_type]+=ex; 
            if(asteroids[a].amount<=0) asteroids[a].active=0; /* Consumed */
            send_server_msg(i,"MINING","Asteroid extraction complete. Minerals transferred to cargo."); f=true; break; 
        }
    }
    
    if(!f) send_server_msg(i,"COMPUTER", "No planet or asteroid in range for mining.");
}

void handle_sco(int i, const char *params) {
    bool near=false; for(int s=0; s<MAX_STARS; s++) if(stars_data[s].active && stars_data[s].q1==players[i].state.q1 && stars_data[s].q2==players[i].state.q2 && stars_data[s].q3==players[i].state.q3) {
        double d=sqrt(pow(stars_data[s].x-players[i].state.s1,2)+pow(stars_data[s].y-players[i].state.s2,2)+pow(stars_data[s].z-players[i].state.s3,2)); if(d<2.0) { near=true; break; }
    }
    if(near) { players[i].state.cargo_energy += 5000; if(players[i].state.cargo_energy > 1000000) players[i].state.cargo_energy = 1000000; int s_idx = rand()%6; players[i].state.shields[s_idx] -= 500; if(players[i].state.shields[s_idx]<0) players[i].state.shields[s_idx]=0; send_server_msg(i, "ENGINEERING", "Solar energy stored."); } 
    else send_server_msg(i, "COMPUTER", "No star in range.");
}

void handle_har(int i, const char *params) {
    bool near=false; for(int h=0; h<MAX_BH; h++) if(black_holes[h].active && black_holes[h].q1==players[i].state.q1 && black_holes[h].q2==players[i].state.q2 && black_holes[h].q3==players[i].state.q3) {
        double d=sqrt(pow(black_holes[h].x-players[i].state.s1,2)+pow(black_holes[h].y-players[i].state.s2,2)+pow(black_holes[h].z-players[i].state.s3,2)); if(d<2.0) { near=true; break; }
    }
    if(near) { players[i].state.cargo_energy += 10000; if(players[i].state.cargo_energy > 1000000) players[i].state.cargo_energy = 1000000; players[i].state.inventory[1] += 100; int s_idx = rand()%6; players[i].state.shields[s_idx] -= 1000; if(players[i].state.shields[s_idx]<0) players[i].state.shields[s_idx]=0; send_server_msg(i, "ENGINEERING", "Antimatter stored."); } 
    else send_server_msg(i, "COMPUTER", "No black hole in range.");
}

void handle_doc(int i, const char *params) {
    bool near=false; for(int b=0; b<MAX_BASES; b++) if(bases[b].active && bases[b].q1==players[i].state.q1 && bases[b].q2==players[i].state.q2 && bases[b].q3==players[i].state.q3) {
        double d=sqrt(pow(bases[b].x-players[i].state.s1,2)+pow(bases[b].y-players[i].state.s2,2)+pow(bases[b].z-players[i].state.s3,2)); if(d<2.0) { near=true; break; } 
    }
    if(near) { 
        players[i].state.energy=1000000; 
        players[i].state.torpedoes=1000; 
        players[i].state.cargo_energy=1000000;
        players[i].state.cargo_torpedoes=1000;
        for(int s=0; s<8; s++) players[i].state.system_health[s]=100.0f; 
        send_server_msg(i, "STARBASE", "Docking complete. Reactor and Cargo Bay replenished."); 
    } 
    else send_server_msg(i,"COMPUTER","No starbase in range.");
}

void handle_con(int i, const char *params) {
    int t,a; 
    if(sscanf(params," %d %d",&t,&a)==2 && t>=1 && t<=6 && players[i].state.inventory[t]>=a) {
        players[i].state.inventory[t]-=a; 
        if(t==1) { players[i].state.cargo_energy+=a*10; } 
        else if(t==2) { players[i].state.cargo_energy+=a*2; } 
        else if(t==3) { players[i].state.cargo_torpedoes+=a/20; } 
        else if(t==6) { players[i].state.cargo_energy+=a*5; }
        
        if(players[i].state.cargo_energy>1000000) players[i].state.cargo_energy=1000000; 
        if(players[i].state.cargo_torpedoes>1000) players[i].state.cargo_torpedoes=1000;
        
        send_server_msg(i,"ENGINEERING","Assets stored in Cargo Bay.");
    }
}

void handle_load(int i, const char *params) {
    int type, amount; if (sscanf(params, "%d %d", &type, &amount) == 2) {
        if (type == 1) { if(amount>players[i].state.cargo_energy) amount=players[i].state.cargo_energy; players[i].state.cargo_energy-=amount; players[i].state.energy+=amount; if(players[i].state.energy>1000000) players[i].state.energy=1000000; send_server_msg(i,"ENGINEERING","Energy loaded."); }
        else if (type == 2) { if(amount>players[i].state.cargo_torpedoes) amount=players[i].state.cargo_torpedoes; players[i].state.cargo_torpedoes-=amount; players[i].state.torpedoes+=amount; if(players[i].state.torpedoes>1000) players[i].state.torpedoes=1000; send_server_msg(i,"TACTICAL","Torps loaded."); }
    }
}

void handle_psy(int i, const char *params) {
    if (players[i].state.corbomite_count > 0) {
        send_server_msg(i, "COMMANDER", "Broadcasting Corbomite threat on all frequencies...");
        int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
        QuadrantIndex *local_q = &spatial_index[pq1][pq2][pq3];
        
        if (rand() % 100 < 60) { /* 60% success rate */
            for (int n = 0; n < local_q->npc_count; n++) {
                local_q->npcs[n]->ai_state = AI_STATE_FLEE;
                local_q->npcs[n]->energy += 5000; /* Give them a 'panic' boost to run away */
            }
            send_server_msg(i, "SCIENCE", "Bluff successful. Hostile vessels are breaking formation!");
        } else {
            send_server_msg(i, "TACTICAL", "The enemy is ignoring our broadcast. Bluff failed.");
        }
        players[i].state.corbomite_count--;
    } else {
        send_server_msg(i, "COMPUTER", "No Corbomite devices available in inventory.");
    }
}

void handle_supernova(int i, const char *params) {
    if (supernova_event.supernova_timer > 0) {
        send_server_msg(i, "COMPUTER", "A supernova event is already in progress.");
        return;
    }
    int q1 = players[i].state.q1, q2 = players[i].state.q2, q3 = players[i].state.q3;
    supernova_event.supernova_q1 = q1;
    supernova_event.supernova_q2 = q2;
    supernova_event.supernova_q3 = q3;
    supernova_event.supernova_timer = 1800; /* 60 seconds */
    
    /* Find a star to explode */
    supernova_event.x = 5.0; supernova_event.y = 5.0; supernova_event.z = 5.0;
    QuadrantIndex *qi = &spatial_index[q1][q2][q3];
    if (qi->star_count > 0) {
        supernova_event.x = qi->stars[0]->x;
        supernova_event.y = qi->stars[0]->y;
        supernova_event.z = qi->stars[0]->z;
    }

    send_server_msg(i, "ADMIN", "SUPERNOVA INITIATED IN CURRENT QUADRANT.");
}

void handle_rep(int i, const char *params) {
    int sid; if(sscanf(params," %d",&sid)==1 && sid>=0 && sid<8) {
        bool can=false;
        if(players[i].state.inventory[2]>=50 && players[i].state.inventory[5]>=10) {
            players[i].state.inventory[2]-=50; players[i].state.inventory[5]-=10;
            can=true;
        }
        if(can){ players[i].state.system_health[sid]=100.0f; send_server_msg(i,"ENGINEERING","Repairs complete."); }
        else send_server_msg(i,"ENGINEERING","Insufficient materials.");
    }
}

void handle_sta(int i, const char *params) {
    char b[4096]; const char* f_name = get_species_name(players[i].faction);
    const char* c_names[] = {"Constitution", "Miranda", "Excelsior", "Constellation", "Defiant", "Galaxy", "Sovereign", "Intrepid", "Akira", "Nebula", "Ambassador", "Oberth", "Steamrunner", "Vessel"};
    const char* class_name = (players[i].ship_class >= 0 && players[i].ship_class <= 13) ? c_names[players[i].ship_class] : "Unknown";
    snprintf(b, sizeof(b), CYAN "\n.--- LCARS MAIN COMPUTER: SHIP DIAGNOSTICS -----------------------.\n" RESET WHITE " COMMANDER: %-18s CLASS: %-15s\n FACTION:   %-18s STATUS: %s\n CREW COMPLEMENT: %d\n" RESET, players[i].name, class_name, f_name, players[i].state.is_cloaked ? MAGENTA "[ CLOAKED ]" RESET : GREEN "[ ACTIVE ]" RESET, players[i].state.crew_count);
    strcat(b, BLUE "\n[ POSITION AND TELEMETRY ]\n" RESET);
    snprintf(b+strlen(b), sizeof(b)-strlen(b), " QUADRANT: [%d,%d,%d]  SECTOR: [%.2f, %.2f, %.2f]\n", players[i].state.q1, players[i].state.q2, players[i].state.q3, players[i].state.s1, players[i].state.s2, players[i].state.s3);
    snprintf(b+strlen(b), sizeof(b)-strlen(b), " HEADING:  %03.0f\302\260        MARK:   %+03.0f\302\260\n", players[i].state.ent_h, players[i].state.ent_m);
    snprintf(b+strlen(b), sizeof(b)-strlen(b), " NAV MODE: %s\n", (players[i].nav_state == NAV_STATE_CHASE) ? B_RED "[ CHASE ACTIVE ]" RESET : "[ NORMAL ]");
    strcat(b, BLUE "\n[ POWER AND REACTOR STATUS ]\n" RESET);
    float en_pct = (players[i].state.energy / 1000000.0f) * 100.0f; char en_bar[21]; int en_fills = (int)(en_pct / 5); for(int j=0; j<20; j++) en_bar[j] = (j < en_fills) ? '|' : '-'; en_bar[20] = '\0';
    snprintf(b+strlen(b), sizeof(b)-strlen(b), " MAIN REACTOR: [%s] %d / 1000000 (%.1f%%)\n ALLOCATION:   ENGINES: %.0f%%  SHIELDS: %.0f%%  WEAPONS: %.0f%%\n", en_bar, players[i].state.energy, en_pct, players[i].state.power_dist[0]*100, players[i].state.power_dist[1]*100, players[i].state.power_dist[2]*100);
    strcat(b, YELLOW "[ CARGO BAY - LOGISTICS ]\n" RESET);
    snprintf(b+strlen(b), sizeof(b)-strlen(b), " STORED ENERGY: %-6d  STORED TORPS: %-3d\n", players[i].state.cargo_energy, players[i].state.cargo_torpedoes);
    strcat(b, YELLOW "[ STORED MINERALS & RESOURCES ]\n" RESET);
    snprintf(b+strlen(b), sizeof(b)-strlen(b), " DILITHIUM:  %-5d  TRITANIUM:  %-5d  VERTERIUM: %-5d\n", players[i].state.inventory[1], players[i].state.inventory[2], players[i].state.inventory[3]);
    snprintf(b+strlen(b), sizeof(b)-strlen(b), " MONOTANIUM: %-5d  ISOLINEAR:  %-5d  GASES:     %-5d\n", players[i].state.inventory[4], players[i].state.inventory[5], players[i].state.inventory[6]);
    strcat(b, BLUE "\n[ DEFENSIVE GRID AND ARMAMENTS ]\n" RESET);
    snprintf(b+strlen(b), sizeof(b)-strlen(b), " SHIELDS: F:%-4d R:%-4d T:%-4d B:%-4d L:%-4d RI:%-4d\n PHOTON TORPEDOES: %-2d  LOCK: %s\n", players[i].state.shields[0], players[i].state.shields[1], players[i].state.shields[2], players[i].state.shields[3], players[i].state.shields[4], players[i].state.shields[5], players[i].state.torpedoes, (players[i].state.lock_target > 0) ? RED "[ LOCKED ]" RESET : "[ NONE ]");
    strcat(b, BLUE "\n[ SYSTEMS INTEGRITY ]\n" RESET);
    const char* sys[] = {"Warp","Imp","Sens","Tran","Phas","Torp","Comp","Life"};
    for(int s=0; s<8; s++) { float hp = players[i].state.system_health[s]; const char* col = (hp > 75) ? GREEN : (hp > 25) ? YELLOW : RED; snprintf(b+strlen(b), sizeof(b)-strlen(b), " %-8s: %s%5.1f%%" RESET " ", sys[s], col, hp); if (s == 3) strcat(b, "\n"); }
    strcat(b, CYAN "\n'-----------------------------------------------------------------'\n" RESET);
    send_server_msg(i, "COMPUTER", b);
}

void handle_inv(int i, const char *params) {
    char b[512]=YELLOW "\n--- CARGO MANIFEST ---\n" RESET; char it[64]; const char* r[]={"-","Dilithium","Tritanium","Verterium","Monotanium","Isolinear","Gases"};
    for(int j=1; j<=6; j++){ sprintf(it," %-12s: %-4d\n",r[j],players[i].state.inventory[j]); strcat(b,it); }
    snprintf(it, sizeof(it), BLUE " Stored Energy: %d\n Stored Torps:  %d\n" RESET, players[i].state.cargo_energy, players[i].state.cargo_torpedoes); strcat(b, it);
    send_server_msg(i, "LOGISTICS", b);
}

void handle_dam(int i, const char *params) {
    char b[512]=RED "\n--- DAMAGE REPORT ---" RESET; char sbuf[64]; const char* sys[]={"Warp","Impulse","Sensors","Transp","Phasers","Torps","Computer","Life"};
    for(int s=0; s<8; s++){ sprintf(sbuf," %-10s: %.1f%%\n",sys[s],players[i].state.system_health[s]); strcat(b,sbuf); }
    send_server_msg(i, "ENGINEERING", b);
}

void handle_cal(int i, const char *params) {
    int qx,qy,qz; 
    if(sscanf(params,"%d %d %d",&qx,&qy,&qz)==3) {
        double dx=(qx-players[i].state.q1)*10.0;
        double dy=(qy-players[i].state.q2)*10.0;
        double dz=(qz-players[i].state.q3)*10.0;
        double d=sqrt(dx*dx+dy*dy+dz*dz);
        double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=asin(dz/d)*180/M_PI;
        char buf[256]; sprintf(buf, "Navigation Solution: Heading %.1f, Mark %+.1f, Distance %.2f units.", h, m, d/10.0);
        send_server_msg(i, "COMPUTER", buf);
    }
}

void handle_who(int i, const char *params) {
    char b[1024]=WHITE "\n--- ACTIVE CAPTAINS LOG ---" RESET;
    for(int j=0; j<MAX_CLIENTS; j++) if(players[j].active) {
        char line[128]; sprintf(line, " [%2d] %-18s (Q:%d,%d,%d)\n", j+1, players[j].name, players[j].state.q1, players[j].state.q2, players[j].state.q3);
        strcat(b, line);
    }
    send_server_msg(i, "COMPUTER", b);
}

void handle_aux(int i, const char *params) {
    if(strcmp(params, " jettison") == 0) {
        send_server_msg(i, "ENGINEERING", "WARP CORE EJECTED! MASSIVE ENERGY DISCHARGE DETECTED!");
        players[i].state.energy = 0; players[i].active = 0;
        players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1};
    }
}

void handle_xxx(int i, const char *params) {
    send_server_msg(i, "COMPUTER", "Self-destruct sequence initiated. Zero-zero-zero-destruct-zero.");
    players[i].state.energy = 0; players[i].state.crew_count = 0; players[i].active = 0;
    players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1};
}

void handle_jum(int i, const char *params) {
    int qx, qy, qz;
    if (sscanf(params, "%d %d %d", &qx, &qy, &qz) == 3) {
        if (!IS_Q_VALID(qx, qy, qz)) {
            send_server_msg(i, "COMPUTER", "Invalid quadrant coordinates.");
            return;
        }
        
        /* Cost calculation: 5000 Energy + 1 Dilithium crystal */
        if (players[i].state.energy < 5000 || players[i].state.inventory[1] < 1) {
             send_server_msg(i, "ENGINEERING", "Insufficient resources for Wormhole generation (Req: 5000 Energy, 1 Dilithium).");
             return;
        }

        players[i].state.energy -= 5000;
        players[i].state.inventory[1] -= 1;

        /* Calculate Wormhole Entrance Position (3 units in front of ship) */
        double rad_h = players[i].state.ent_h * M_PI / 180.0;
        double rad_m = players[i].state.ent_m * M_PI / 180.0;
        
        /* Local sector coordinates for the effect */
        double wx = players[i].state.s1 + cos(rad_m) * sin(rad_h) * 3.0;
        double wy = players[i].state.s2 + cos(rad_m) * -cos(rad_h) * 3.0;
        double wz = players[i].state.s3 + sin(rad_m) * 3.0;
        
        players[i].wx = wx; players[i].wy = wy; players[i].wz = wz;

        /* Set Destination */
        players[i].target_gx = (qx - 1) * 10.0 + 5.5; /* Center of target quadrant */
        players[i].target_gy = (qy - 1) * 10.0 + 5.5;
        players[i].target_gz = (qz - 1) * 10.0 + 5.5;
        
        /* Engage */
        players[i].nav_state = NAV_STATE_WORMHOLE;
        players[i].nav_timer = 200; /* Animation duration (approx 3.3 seconds at 60fps logic tick) */
        players[i].warp_speed = 0; /* Stop ship */
        
        send_server_msg(i, "HELMSMAN", "Initiating trans-quadrant jump. Calculating Schwarzschild coordinates...");
    } else {
        send_server_msg(i, "COMPUTER", "Usage: jum <Q1> <Q2> <Q3>");
    }
}

void handle_ical(int i, const char *params) {
    double tx, ty, tz;
    if (sscanf(params, "%lf %lf %lf", &tx, &ty, &tz) == 3) {
        double dx = tx - players[i].state.s1;
        double dy = ty - players[i].state.s2;
        double dz = tz - players[i].state.s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        if (d < 0.001) {
            send_server_msg(i, "COMPUTER", "Target sector matches current position.");
            return;
        }
        double h = atan2(dx, -dy) * 180.0 / M_PI; if (h < 0) h += 360;
        double m = (d > 0.001) ? asin(dz / d) * 180.0 / M_PI : 0;
        char buf[256];
        sprintf(buf, "Impulse Navigation Solution: Heading %.1f, Mark %+.1f, Distance %.2f sector units.", h, m, d);
        send_server_msg(i, "COMPUTER", buf);
    } else {
        send_server_msg(i, "COMPUTER", "Usage: ical <X> <Y> <Z> (Target Sector Coords)");
    }
}

/* --- Command Registry Table --- */

static const CommandDef command_registry[] = {
    {"nav ", handle_nav, "Warp Navigation"},
    {"imp ", handle_imp, "Impulse Drive"},
    {"jum ", handle_jum, "Wormhole Jump"},
    {"apr ", handle_apr, "Approach target"},
    {"cha",  handle_cha, "Chase locked target"},
    {"srs",  handle_srs, "Short Range Sensors"},
    {"lrs",  handle_lrs, "Long Range Sensors"},
    {"pha ", handle_pha, "Fire Phasers <ID> <E> or <E> (Lock)"},
    {"tor",  handle_tor, "Fire Torpedo <H> <M> or auto (Lock)"},
    {"she ", handle_she, "Shield Configuration"},
    {"lock ",handle_lock, "Target Lock-on"},
    {"enc ", handle_enc,  "Encryption Toggle"},
    {"scan ",handle_scan, "Detailed Scan"},
    {"clo",  handle_clo, "Cloaking Device"},
    {"bor",  handle_bor, "Boarding Party"},
    {"dis",  handle_dis, "Dismantle Wreck"},
    {"min",  handle_min, "Planetary Mining"},
    {"sco",  handle_sco, "Solar Scooping"},
    {"har",  handle_har, "Antimatter Harvest"},
    {"doc",  handle_doc, "Dock at Starbase"},
    {"con ", handle_con, "Resource Converter"},
    {"load ",handle_load, "Load Cargo"},
    {"rep ", handle_rep, "Repair Systems"},
    {"sta",  handle_sta, "Status Report"},
    {"inv",  handle_inv, "Inventory Report"},
    {"dam",  handle_dam, "Damage Report"},
    {"cal ", handle_cal, "Warp Calculator"},
    {"ical ",handle_ical,"Impulse Calculator"},
    {"who",  handle_who, "Active Captains List"},
    {"help", handle_help,"Display this directory"},
    {"aux ", handle_aux, "Auxiliary Systems"},
    {"xxx",  handle_xxx, "Self-Destruct"},
    {"supernova", handle_supernova, "Admin: Trigger Supernova"},
    {NULL, NULL, NULL}
};

void handle_help(int i, const char *params) {
    char b[4096] = CYAN "\n--- LCARS COMMAND DIRECTORY ---" RESET;
    for (int c = 0; command_registry[c].name != NULL; c++) {
        char line[128];
        snprintf(line, sizeof(line), WHITE "%-8s" RESET " : %s\n", command_registry[c].name, command_registry[c].description);
        strcat(b, line);
    }
    send_server_msg(i, "COMPUTER", b);
}

void process_command(int i, const char *cmd) {
    pthread_mutex_lock(&game_mutex);
    
    bool found = false;
    for (int c = 0; command_registry[c].name != NULL; c++) {
        size_t len = strlen(command_registry[c].name);
        if (strncmp(cmd, command_registry[c].name, len) == 0) {
            command_registry[c].handler(i, cmd + len);
            found = true;
            break;
        }
    }
    
    if (!found) {
        send_server_msg(i, "COMPUTER", "Invalid command. Type 'help' for assistance.");
    }
    pthread_mutex_unlock(&game_mutex);
}
