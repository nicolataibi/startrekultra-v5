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

/* Type definition for command handlers */
typedef void (*CommandHandler)(int p_idx, const char *params);

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
        players[i].nav_state = NAV_STATE_ALIGN; players[i].nav_timer = 60;
        send_server_msg(i, "HELMSMAN", "Course plotted. Aligning ship.");
    } else {
        send_server_msg(i, "COMPUTER", "Usage: nav <H> <M> <W>");
    }
}

void handle_imp(int i, const char *params) {
    double h, m, s; 
    int parsed = sscanf(params, "%lf %lf %lf", &h, &m, &s);
    if (parsed == 1) { 
        if (sscanf(params, "%lf", &s) == 1) { h = players[i].state.ent_h; m = players[i].state.ent_m; parsed = 3; } 
    }
    if (parsed >= 3) {
        if (s <= 0.0) { 
            players[i].nav_state = NAV_STATE_REALIGN; 
            players[i].nav_timer = 60;
            players[i].start_m = players[i].state.ent_m;
            players[i].warp_speed = 0.0;
            send_server_msg(i, "HELMSMAN", "Impulse All Stop. Stabilizing vector."); 
        } else {
            if (s > 1.0) s = 1.0;
            normalize_upright(&h, &m);
            players[i].target_h = h; players[i].target_m = m;
            players[i].start_h = players[i].state.ent_h; players[i].start_m = players[i].state.ent_m;
            double rad_h = h * M_PI / 180.0; double rad_m = m * M_PI / 180.0;
            players[i].dx = cos(rad_m) * sin(rad_h); players[i].dy = cos(rad_m) * -cos(rad_h); players[i].dz = sin(rad_m);
            players[i].warp_speed = s * 0.5; 
            players[i].nav_state = NAV_STATE_ALIGN_IMPULSE;
            players[i].nav_timer = 60;
            send_server_msg(i, "HELMSMAN", "Course plotted. Aligning ship.");
        }
    } else {
        send_server_msg(i, "COMPUTER", "Usage: imp <H> <M> <S> or imp <S>");
    }
}

void handle_apr(int i, const char *params) {
    int tid; double tdist;
    if (sscanf(params, "%d %lf", &tid, &tdist) == 2) {
        double tx, ty, tz; bool found = false;
        int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
        for(int j=0; j<MAX_CLIENTS; j++) if(players[j].active && (j+1)==tid && players[j].state.q1==pq1 && players[j].state.q2==pq2 && players[j].state.q3==pq3) { tx=(players[j].state.q1-1)*10+players[j].state.s1; ty=(players[j].state.q2-1)*10+players[j].state.s2; tz=(players[j].state.q3-1)*10+players[j].state.s3; found=true; break; }
        if(!found) for(int n=0; n<MAX_NPC; n++) if(npcs[n].active && (n+100)==tid && npcs[n].q1==pq1 && npcs[n].q2==pq2 && npcs[n].q3==pq3) { tx=(npcs[n].q1-1)*10+npcs[n].x; ty=(npcs[n].q2-1)*10+npcs[n].y; tz=(npcs[n].q3-1)*10+npcs[n].z; found=true; break; }
        if(!found) for(int b=0; b<MAX_BASES; b++) if(bases[b].active && (b+500)==tid && bases[b].q1==pq1 && bases[b].q2==pq2 && bases[b].q3==pq3) { tx=(bases[b].q1-1)*10+bases[b].x; ty=(bases[b].q2-1)*10+bases[b].y; tz=(bases[b].q3-1)*10+bases[b].z; found=true; break; }
        if(!found) for(int p=0; p<MAX_PLANETS; p++) if(planets[p].active && (p+1000)==tid && planets[p].q1==pq1 && planets[p].q2==pq2 && planets[p].q3==pq3) { tx=(planets[p].q1-1)*10+planets[p].x; ty=(planets[p].q2-1)*10+planets[p].y; tz=(planets[p].q3-1)*10+planets[p].z; found=true; break; }
        if(!found) for(int s=0; s<MAX_STARS; s++) if(stars_data[s].active && (s+2000)==tid && stars_data[s].q1==pq1 && stars_data[s].q2==pq2 && stars_data[s].q3==pq3) { tx=(stars_data[s].q1-1)*10+stars_data[s].x; ty=(stars_data[s].q2-1)*10+stars_data[s].y; tz=(stars_data[s].q3-1)*10+stars_data[s].z; found=true; break; }
        if(!found) for(int h=0; h<MAX_BH; h++) if(black_holes[h].active && (h+3000)==tid && black_holes[h].q1==pq1 && black_holes[h].q2==pq2 && black_holes[h].q3==pq3) { tx=(black_holes[h].q1-1)*10+black_holes[h].x; ty=(black_holes[h].q2-1)*10+black_holes[h].y; tz=(black_holes[h].q3-1)*10+black_holes[h].z; found=true; break; }
        if(found) {
            double cx=(players[i].state.q1-1)*10+players[i].state.s1; double cy=(players[i].state.q2-1)*10+players[i].state.s2; double cz=(players[i].state.q3-1)*10+players[i].state.s3;
            double dx=tx-cx, dy=ty-cy, dz=tz-cz; double d=sqrt(dx*dx+dy*dy+dz*dz);
            if(d > tdist) {
                double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=asin(dz/d)*180/M_PI;
                players[i].target_h=h; players[i].target_m=m; players[i].dx=dx/d; players[i].dy=dy/d; players[i].dz=dz/d;
                players[i].target_gx=cx+players[i].dx*(d-tdist); players[i].target_gy=cy+players[i].dy*(d-tdist); players[i].target_gz=cz+players[i].dz*(d-tdist);
                players[i].nav_state=NAV_STATE_ALIGN; players[i].nav_timer=60; send_server_msg(i, "HELMSMAN", "Autopilot engaged.");
            } else send_server_msg(i, "COMPUTER", "Target already in range.");
        } else send_server_msg(i, "COMPUTER", "Target not found in current quadrant.");
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
    char b[4096]; int q1=players[i].state.q1, q2=players[i].state.q2, q3=players[i].state.q3; double s1=players[i].state.s1, s2=players[i].state.s2, s3=players[i].state.s3;
    snprintf(b, sizeof(b), CYAN "\n--- SHORT RANGE SENSOR ANALYSIS ---\n" RESET "QUADRANT: [%d,%d,%d] | SECTOR: [%.1f,%.1f,%.1f]\n", q1, q2, q3, s1, s2, s3);
    snprintf(b+strlen(b), sizeof(b)-strlen(b), "ENERGY: %d | TORPEDOES: %d | STATUS: %s\n", players[i].state.energy, players[i].state.torpedoes, players[i].state.is_cloaked ? MAGENTA "CLOAKED" RESET : GREEN "NORMAL" RESET);
    strncat(b, "\nTYPE       ID    POSITION      DIST   H / M         DETAILS\n", sizeof(b)-strlen(b)-1);
    QuadrantIndex *local_q = &spatial_index[q1][q2][q3];
    int locked_id = players[i].state.lock_target;
    bool chasing = (players[i].nav_state == NAV_STATE_CHASE);

    for(int j=0; j<local_q->player_count; j++) {
        ConnectedPlayer *p = local_q->players[j]; if (p == &players[i] || p->state.is_cloaked) continue;
        double dx=p->state.s1-s1, dy=p->state.s2-s2, dz=p->state.s3-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=asin(dz/d)*180/M_PI;
        int pid = (int)(p-players)+1;
        char status[64] = "";
        if (pid == locked_id) { strcat(status, RED "[LOCKED]" RESET); if(chasing) strcat(status, B_RED "[CHASE]" RESET); }
        char line[256]; snprintf(line, sizeof(line), "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     %s (Player) [E:%d] %s\n", "Vessel", pid, p->state.s1, p->state.s2, p->state.s3, d, h, m, p->name, p->state.energy, status); strncat(b, line, sizeof(b)-strlen(b)-1);
    }
    for(int n=0; n<local_q->npc_count; n++) {
        NPCShip *npc = local_q->npcs[n];
        double dx=npc->x-s1, dy=npc->y-s2, dz=npc->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=asin(dz/d)*180/M_PI;
        int nid = npc->id+100;
        char status[64] = "";
        if (nid == locked_id) { strcat(status, RED "[LOCKED]" RESET); if(chasing) strcat(status, B_RED "[CHASE]" RESET); }
        char line[256]; snprintf(line, sizeof(line), "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     %s [E:%d] [Engines:%.0f%%] %s\n", "Vessel", nid, npc->x, npc->y, npc->z, d, h, m, get_species_name(npc->faction), npc->energy, npc->engine_health, status); strncat(b, line, sizeof(b)-strlen(b)-1);
    }
    for(int p=0; p<local_q->planet_count; p++) {
        NPCPlanet *pl = local_q->planets[p];
        double dx=pl->x-s1, dy=pl->y-s2, dz=pl->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int plid = pl->id+1000;
        char status[64] = "";
        if (plid == locked_id) { strcat(status, RED "[LOCKED]" RESET); if(chasing) strcat(status, B_RED "[CHASE]" RESET); }
        char line[256]; snprintf(line, sizeof(line), "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Class-M Planet %s\n", "Planet", plid, pl->x, pl->y, pl->z, d, h, m, status); strncat(b, line, sizeof(b)-strlen(b)-1);
    }
    for(int s=0; s<local_q->star_count; s++) {
        NPCStar *st = local_q->stars[s];
        double dx=st->x-s1, dy=st->y-s2, dz=st->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int sid = st->id+2000;
        char status[64] = "";
        if (sid == locked_id) { strcat(status, RED "[LOCKED]" RESET); if(chasing) strcat(status, B_RED "[CHASE]" RESET); }
        char line[256]; snprintf(line, sizeof(line), "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Star %s\n", "Star", sid, st->x, st->y, st->z, d, h, m, status); strncat(b, line, sizeof(b)-strlen(b)-1);
    }
    for(int h_idx=0; h_idx<local_q->bh_count; h_idx++) {
        NPCBlackHole *bh = local_q->black_holes[h_idx];
        double dx=bh->x-s1, dy=bh->y-s2, dz=bh->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double hh=atan2(dx,-dy)*180/M_PI; if(hh<0)hh+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int bid = bh->id+3000;
        char status[64] = "";
        if (bid == locked_id) { strcat(status, RED "[LOCKED]" RESET); if(chasing) strcat(status, B_RED "[CHASE]" RESET); }
        char line[256]; snprintf(line, sizeof(line), "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Black Hole %s\n", "B-Hole", bid, bh->x, bh->y, bh->z, d, hh, m, status); strncat(b, line, sizeof(b)-strlen(b)-1);
    }
    send_server_msg(i, "COMPUTER", b);
}

void handle_lrs(int i, const char *params) {
    char rep[4096] = YELLOW "\n--- 3D LONG RANGE SENSOR SCAN ---" RESET; char line[512]; int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3; double ps1 = players[i].state.s1, ps2 = players[i].state.s2, ps3 = players[i].state.s3;
    for (int l = pq3 + 1; l >= pq3 - 1; l--) { if (l < 1 || l > 10) continue; snprintf(line, sizeof(line), WHITE "\n[ DECK Z:%d ]\n" RESET, l); strncat(rep, line, sizeof(rep)-strlen(rep)-1); strncat(rep, "         X-1 (West)               X (Center)               X+1 (East)\n", sizeof(rep)-strlen(rep)-1);
        for (int y = pq2 - 1; y <= pq2 + 1; y++) {
            if (y == pq2 - 1) strncat(rep, "Y-1 (N) ", sizeof(rep)-strlen(rep)-1); else if (y == pq2) strncat(rep, "Y   (C) ", sizeof(rep)-strlen(rep)-1); else strncat(rep, "Y+1 (S) ", sizeof(rep)-strlen(rep)-1);
            for (int x = pq1 - 1; x <= pq1 + 1; x++) {
                if (x >= 1 && x <= 10 && y >= 1 && y <= 10) {
                    QuadrantIndex *qi = &spatial_index[x][y][l]; int val = qi->bh_count * 10000 + qi->planet_count * 1000 + (qi->npc_count + qi->player_count) * 100 + qi->base_count * 10 + qi->star_count; int h = -1;
                    if (y == pq2 - 1) { if (x == pq1 - 1) h = 315; else if (x == pq1) h = 0; else h = 45; } else if (y == pq2) { if (x == pq1 - 1) h = 270; else if (x == pq1 + 1) h = 90; } else if (y == pq2 + 1) { if (x == pq1 - 1) h = 225; else if (x == pq1) h = 180; else h = 135; }
                    double dx_s = (x - pq1) * 10.0 + (5.5 - ps1); double dy_s = (pq2 - y) * 10.0 + (ps2 - 5.5); double dz_s = (l - pq3) * 10.0 + (5.5 - ps3); double dist_s = sqrt(dx_s*dx_s + dy_s*dy_s + dz_s*dz_s); double w_req = dist_s / 10.0; int m = (dist_s > 0.001) ? (int)(asin(dz_s / dist_s) * 180.0 / M_PI) : 0;
                    if (x == pq1 && y == pq2 && l == pq3) strncat(rep, ":[        " BLUE "YOU" RESET "         ]: ", sizeof(rep)-strlen(rep)-1); else { snprintf(line, sizeof(line), "[%05d/H%03d/M%+03d/W%.1f]: ", val, (h==-1?0:h), m, w_req); strncat(rep, line, sizeof(rep)-strlen(rep)-1); }
                } else strncat(rep, ":[        ***         ]: ", sizeof(rep)-strlen(rep)-1);
            }
            strncat(rep, "\n", sizeof(rep)-strlen(rep)-1);
        }
    }
    send_server_msg(i, "SCIENCE", rep);
}

void handle_pha(int i, const char *params) {
    int e; 
    if(sscanf(params, "%d", &e) == 1) {
        if (players[i].state.energy < e) {
            send_server_msg(i, "COMPUTER", "Insufficient energy for phaser burst.");
        } else if (players[i].state.system_health[4] < 10.0f) {
            send_server_msg(i, "WARNING", "Phaser banks inoperative.");
        } else {
            players[i].state.energy -= e; 
            players[i].state.beam_count = 1; 
            players[i].state.beams[0].active = 1;
            int tid = players[i].state.lock_target; 
            double tx, ty, tz; 
            bool found = false;
            int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
            if (tid >= 1 && tid <= 32 && players[tid-1].active && players[tid-1].state.q1 == pq1 && players[tid-1].state.q2 == pq2 && players[tid-1].state.q3 == pq3) { tx = players[tid-1].state.s1; ty = players[tid-1].state.s2; tz = players[tid-1].state.s3; found = true; }
            else if (tid >= 100 && tid < 100+MAX_NPC && npcs[tid-100].active && npcs[tid-100].q1 == pq1 && npcs[tid-100].q2 == pq2 && npcs[tid-100].q3 == pq3) { tx = npcs[tid-100].x; ty = npcs[tid-100].y; tz = npcs[tid-100].z; found = true; }
            if (!found) { tx = players[i].state.s1 + cos(players[i].state.ent_m*M_PI/180)*sin(players[i].state.ent_h*M_PI/180)*5; ty = players[i].state.s2 + cos(players[i].state.ent_m*M_PI/180)*-cos(players[i].state.ent_h*M_PI/180)*5; tz = players[i].state.s3 + sin(players[i].state.ent_m*M_PI/180)*5; }
            players[i].state.beams[0].net_tx = tx; players[i].state.beams[0].net_ty = ty; players[i].state.beams[0].net_tz = tz;
            double dist = sqrt(pow(tx-players[i].state.s1,2)+pow(ty-players[i].state.s2,2)+pow(tz-players[i].state.s3,2)); 
            if (dist < 0.1) dist = 0.1;
            float p_boost = 0.5f + (players[i].state.power_dist[2] * 1.0f); 
            float integrity = (players[i].state.system_health[4] / 100.0f);
            int hit = (int)((e / dist) * p_boost * integrity * 10.0f);
            if (found) {
                if (tid <= 32) {
                    int dmg_rem = hit;
                    for (int s=0; s<6; s++) { if (dmg_rem <= 0) break; int abs = (players[tid-1].state.shields[s] >= dmg_rem/6) ? dmg_rem/6 : players[tid-1].state.shields[s]; players[tid-1].state.shields[s] -= abs; dmg_rem -= abs; }
                    players[tid-1].state.energy -= dmg_rem;
                    if (players[tid-1].state.energy <= 0) { players[tid-1].active = 0; players[tid-1].state.boom = (NetPoint){(float)players[tid-1].state.s1, (float)players[tid-1].state.s2, (float)players[tid-1].state.s3, 1}; }
                } else if (tid >= 100 && tid < 500) {
                    npcs[tid-100].energy -= hit; float engine_dmg = (hit / 1000.0f) * 10.0f; npcs[tid-100].engine_health -= engine_dmg; if (npcs[tid-100].engine_health < 0) npcs[tid-100].engine_health = 0;
                    if (npcs[tid-100].energy <= 0) { npcs[tid-100].active = 0; players[i].state.boom = (NetPoint){(float)npcs[tid-100].x, (float)npcs[tid-100].y, (float)npcs[tid-100].z, 1}; }
                }
                char hit_msg[64]; sprintf(hit_msg, "Phasers hit target! Damage: %d", hit); send_server_msg(i, "TACTICAL", hit_msg);
            } else { send_server_msg(i, "TACTICAL", "Phasers fired into space."); }
        }
    }
}

void handle_tor(int i, const char *params) {
    double h, m; bool manual = true; int tid = players[i].state.lock_target;
    if (tid > 0) {
        double tx, ty, tz; bool found=false;
        if(tid<=32 && players[tid-1].active) { tx=players[tid-1].state.s1; ty=players[tid-1].state.s2; tz=players[tid-1].state.s3; found=true; }
        else if(tid>=100 && tid<100+MAX_NPC && npcs[tid-100].active) { tx=npcs[tid-100].x; ty=npcs[tid-100].y; tz=npcs[tid-100].z; found=true; }
        if(found) { double dx=tx-players[i].state.s1, dy=ty-players[i].state.s2, dz=tz-players[i].state.s3; h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; m=asin(dz/sqrt(dx*dx+dy*dy+dz*dz))*180/M_PI; manual=false; }
    }
    if(manual && sscanf(params, "%lf %lf", &h, &m)!=2) manual=false;
    if((!manual || tid>0) && players[i].state.torpedoes > 0) {
        players[i].state.torpedoes--; players[i].torp_active=true; 
        players[i].torp_target = (manual) ? 0 : tid;
        players[i].tx=players[i].state.s1; players[i].ty=players[i].state.s2; players[i].tz=players[i].state.s3;
        players[i].tdx=cos(m*M_PI/180)*sin(h*M_PI/180); players[i].tdy=cos(m*M_PI/180)*-cos(h*M_PI/180); players[i].tdz=sin(m*M_PI/180);
        send_server_msg(i, "TACTICAL", manual ? "Torpedo away (Manual)." : "Torpedo away (Locked).");
    }
}

void handle_she(int i, const char *params) {
    int f,r,t,b,l,ri; 
    if(sscanf(params, "%d %d %d %d %d %d", &f,&r,&t,&b,&l,&ri) == 6) {
        players[i].state.shields[0]=f; players[i].state.shields[1]=r; players[i].state.shields[2]=t; players[i].state.shields[3]=b; players[i].state.shields[4]=l; players[i].state.shields[5]=ri;
        send_server_msg(i, "ENGINEERING", "Shields updated.");
    }
}

void handle_lock(int i, const char *params) {
    int tid = 0;
    if (sscanf(params, "%d", &tid) == 1) {
        if (tid == 0) {
            players[i].state.lock_target = 0;
            send_server_msg(i, "TACTICAL", "Lock released.");
        } else {
            bool found = false; char target_name[64] = "Unknown Target";
            int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
            if (tid >= 1 && tid <= MAX_CLIENTS) { if (players[tid-1].active && players[tid-1].state.q1 == pq1 && players[tid-1].state.q2 == pq2 && players[tid-1].state.q3 == pq3) { found = true; snprintf(target_name, sizeof(target_name), "%s", players[tid-1].name); } }
            else if (tid >= 100 && tid < 100+MAX_NPC) { if (npcs[tid-100].active && npcs[tid-100].q1 == pq1 && npcs[tid-100].q2 == pq2 && npcs[tid-100].q3 == pq3) { found = true; snprintf(target_name, sizeof(target_name), "%s Vessel", get_species_name(npcs[tid-100].faction)); } }
            else if (tid >= 500 && tid < 500+MAX_BASES) { if (bases[tid-500].active && bases[tid-500].q1 == pq1 && bases[tid-500].q2 == pq2 && bases[tid-500].q3 == pq3) { found = true; strcpy(target_name, "Starbase"); } }
            else if (tid >= 1000 && tid < 1000+MAX_PLANETS) { if (planets[tid-1000].active && planets[tid-1000].q1 == pq1 && planets[tid-1000].q2 == pq2 && planets[tid-1000].q3 == pq3) { found = true; strcpy(target_name, "Planet"); } }
            if (found) { players[i].state.lock_target = tid; char msg[128]; snprintf(msg, sizeof(msg), "Target locked: %s (ID %d)", target_name, tid); send_server_msg(i, "TACTICAL", msg); } else { send_server_msg(i, "COMPUTER", "Unable to acquire lock. Target not found."); }
        }
    }
}

void handle_clo(int i, const char *params) {
    players[i].state.is_cloaked = !players[i].state.is_cloaked;
    send_server_msg(i, "ENGINEERING", players[i].state.is_cloaked ? MAGENTA "Cloak active." RESET : GREEN "Cloak offline." RESET);
}

void handle_bor(int i, const char *params) {
    if (players[i].state.energy < 5000) { send_server_msg(i, "COMPUTER", "Insufficient energy for boarding operation."); return; }
    int tid = players[i].state.lock_target;
    double tx, ty, tz; bool found = false;
    int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
    if (tid >= 1 && tid <= 32 && players[tid-1].active && players[tid-1].state.q1 == pq1 && players[tid-1].state.q2 == pq2 && players[tid-1].state.q3 == pq3) { tx = players[tid-1].state.s1; ty = players[tid-1].state.s2; tz = players[tid-1].state.s3; found = true; }
    else if (tid >= 100 && tid < 500 && npcs[tid-100].active && npcs[tid-100].q1 == pq1 && npcs[tid-100].q2 == pq2 && npcs[tid-100].q3 == pq3) { tx = npcs[tid-100].x; ty = npcs[tid-100].y; tz = npcs[tid-100].z; found = true; }
    if (found) {
        double d = sqrt(pow(tx-players[i].state.s1,2)+pow(ty-players[i].state.s2,2)+pow(tz-players[i].state.s3,2));
        if (d < 1.0) {
            players[i].state.energy -= 5000;
            if (rand()%100 < 80) {
                if (tid <= 32) { for(int s=0; s<8; s++) players[tid-1].state.system_health[s] *= 0.5f; players[tid-1].nav_state = NAV_STATE_IDLE; send_server_msg(tid, "CRITICAL", "ENEMY BOARDING PARTIES ON ALL DECKS!"); } 
                else { npcs[tid-100].engine_health = 0; npcs[tid-100].energy *= 0.7; }
                send_server_msg(i, "TACTICAL", "Boarding successful. Enemy systems disabled.");
            } else { send_server_msg(i, "SECURITY", "Boarding party repelled! We sustained internal damage."); }
        } else send_server_msg(i, "COMPUTER", "Target out of transporter range.");
    }
}

void handle_dis(int i, const char *params) {
    int tid = players[i].state.lock_target;
    double tx, ty, tz; bool found = false;
    int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
    if (tid >= 100 && tid < 500 && npcs[tid-100].active && npcs[tid-100].q1 == pq1 && npcs[tid-100].q2 == pq2 && npcs[tid-100].q3 == pq3) { tx = npcs[tid-100].x; ty = npcs[tid-100].y; tz = npcs[tid-100].z; found = true; }
    if (found) {
        double d = sqrt(pow(tx-players[i].state.s1,2)+pow(ty-players[i].state.s2,2)+pow(tz-players[i].state.s3,2));
        if (d < 1.5) {
            if (npcs[tid-100].engine_health > 10.0f) send_server_msg(i, "COMPUTER", "Cannot dismantle active vessel. Disable engines first (use bor).");
            else {
                players[i].state.dismantle = (NetDismantle){(float)tx, (float)ty, (float)tz, npcs[tid-100].faction, 1};
                int yield = (npcs[tid-100].energy / 100); players[i].state.inventory[2] += yield; players[i].state.inventory[5] += yield / 5; npcs[tid-100].active = 0;
                send_server_msg(i, "ENGINEERING", "Vessel dismantled. Resources recovered.");
            }
        } else send_server_msg(i, "COMPUTER", "Target out of tractor beam range.");
    }
}

void handle_min(int i, const char *params) {
    bool f=false; for(int p=0;p<MAX_PLANETS;p++) if(planets[p].active && planets[p].q1==players[i].state.q1 && planets[p].q2==players[i].state.q2 && planets[p].q3==players[i].state.q3) {
        double d=sqrt(pow(planets[p].x-players[i].state.s1,2)+pow(planets[p].y-players[i].state.s2,2)+pow(planets[p].z-players[i].state.s3,2));
        if(d<2.0){ int ex=(planets[p].amount>100)?100:planets[p].amount; planets[p].amount-=ex; players[i].state.inventory[planets[p].resource_type]+=ex; send_server_msg(i,"GEOLOGY","Mining successful."); f=true; break; }
    }
    if(!f) send_server_msg(i,"COMPUTER", "No planet in range.");
}

void handle_sco(int i, const char *params) {
    bool near=false; for(int s=0; s<MAX_STARS; s++) if(stars_data[s].active && stars_data[s].q1==players[i].state.q1 && stars_data[s].q2==players[i].state.q2 && stars_data[s].q3==players[i].state.q3) {
        double d=sqrt(pow(stars_data[s].x-players[i].state.s1,2)+pow(stars_data[s].y-players[i].state.s2,2)+pow(stars_data[s].z-players[i].state.s3,2)); if(d<2.0) { near=true; break; }
    }
    if(near) { players[i].state.cargo_energy += 5000; if(players[i].state.cargo_energy > 100000) players[i].state.cargo_energy = 100000; int s_idx = rand()%6; players[i].state.shields[s_idx] -= 500; if(players[i].state.shields[s_idx]<0) players[i].state.shields[s_idx]=0; send_server_msg(i, "ENGINEERING", "Solar energy stored."); } 
    else send_server_msg(i, "COMPUTER", "No star in range.");
}

void handle_har(int i, const char *params) {
    bool near=false; for(int h=0; h<MAX_BH; h++) if(black_holes[h].active && black_holes[h].q1==players[i].state.q1 && black_holes[h].q2==players[i].state.q2 && black_holes[h].q3==players[i].state.q3) {
        double d=sqrt(pow(black_holes[h].x-players[i].state.s1,2)+pow(black_holes[h].y-players[i].state.s2,2)+pow(black_holes[h].z-players[i].state.s3,2)); if(d<2.0) { near=true; break; }
    }
    if(near) { players[i].state.cargo_energy += 10000; if(players[i].state.cargo_energy > 100000) players[i].state.cargo_energy = 100000; players[i].state.inventory[1] += 100; int s_idx = rand()%6; players[i].state.shields[s_idx] -= 1000; if(players[i].state.shields[s_idx]<0) players[i].state.shields[s_idx]=0; send_server_msg(i, "ENGINEERING", "Antimatter stored."); } 
    else send_server_msg(i, "COMPUTER", "No black hole in range.");
}

void handle_doc(int i, const char *params) {
    bool near=false; for(int b=0; b<MAX_BASES; b++) if(bases[b].active && bases[b].q1==players[i].state.q1 && bases[b].q2==players[i].state.q2 && bases[b].q3==players[i].state.q3) {
        double d=sqrt(pow(bases[b].x-players[i].state.s1,2)+pow(bases[b].y-players[i].state.s2,2)+pow(bases[b].z-players[i].state.s3,2)); if(d<2.0) { near=true; break; } 
    }
    if(near) { players[i].state.energy=100000; players[i].state.torpedoes=100; for(int s=0;s<8;s++) players[i].state.system_health[s]=100.0f; send_server_msg(i, "STARBASE", "Docking complete."); } 
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
        
        if(players[i].state.cargo_energy>100000) players[i].state.cargo_energy=100000; 
        if(players[i].state.cargo_torpedoes>100) players[i].state.cargo_torpedoes=100;
        
        send_server_msg(i,"ENGINEERING","Assets stored in Cargo Bay.");
    }
}

void handle_load(int i, const char *params) {
    int type, amount; if (sscanf(params, "%d %d", &type, &amount) == 2) {
        if (type == 1) { if(amount>players[i].state.cargo_energy) amount=players[i].state.cargo_energy; players[i].state.cargo_energy-=amount; players[i].state.energy+=amount; if(players[i].state.energy>100000) players[i].state.energy=100000; send_server_msg(i,"ENGINEERING","Energy loaded."); }
        else if (type == 2) { if(amount>players[i].state.cargo_torpedoes) amount=players[i].state.cargo_torpedoes; players[i].state.cargo_torpedoes-=amount; players[i].state.torpedoes+=amount; if(players[i].state.torpedoes>100) players[i].state.torpedoes=100; send_server_msg(i,"TACTICAL","Torps loaded."); }
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

void handle_xxx(int i, const char *params) {
    send_server_msg(i, "CRITICAL", "SELF-DESTRUCT INITIATED. GODSPEED, CAPTAIN.");
    players[i].active = 0;
    players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1};
    /* Radial damage logic could be added here in logic.c */
}

void handle_rep(int i, const char *params) {
    int sid; if(sscanf(params,"%d",&sid)==1 && sid>=0 && sid<8) {
        bool can=false; if(sid==0||sid==1||sid==5||sid==7){ if(players[i].state.inventory[4]>=50){ players[i].state.inventory[4]-=50; can=true; } } 
        else { if(players[i].state.inventory[5]>=30){ players[i].state.inventory[5]-=30; can=true; } }
        if(can){ players[i].state.system_health[sid]=100.0f; send_server_msg(i,"ENGINEERING","Repairs complete."); } 
        else send_server_msg(i,"ENGINEERING","Insufficient materials.");
    }
}

void handle_sta(int i, const char *params) {
    char b[4096]; const char* f_name = get_species_name(players[i].faction);
    const char* c_names[] = {"Constitution", "Miranda", "Excelsior", "Constellation", "Defiant", "Galaxy", "Sovereign", "Intrepid", "Akira", "Nebula", "Ambassador", "Oberth", "Steamrunner", "Vessel"};
    const char* class_name = (players[i].ship_class >= 0 && players[i].ship_class <= 13) ? c_names[players[i].ship_class] : "Unknown";
    snprintf(b, sizeof(b), CYAN "\n.--- LCARS MAIN COMPUTER: SHIP DIAGNOSTICS -----------------------.\n" RESET WHITE " COMMANDER: %-18s CLASS: %-15s\n FACTION:   %-18s STATUS: %s\n" RESET, players[i].name, class_name, f_name, players[i].state.is_cloaked ? MAGENTA "[ CLOAKED ]" RESET : GREEN "[ ACTIVE ]" RESET);
    strcat(b, BLUE "\n[ POSITION AND TELEMETRY ]\n" RESET);
    snprintf(b+strlen(b), sizeof(b)-strlen(b), " QUADRANT: [%d,%d,%d]  SECTOR: [%.2f, %.2f, %.2f]\n", players[i].state.q1, players[i].state.q2, players[i].state.q3, players[i].state.s1, players[i].state.s2, players[i].state.s3);
    snprintf(b+strlen(b), sizeof(b)-strlen(b), " HEADING:  %03.0f\302\260        MARK:   %+03.0f\302\260\n", players[i].state.ent_h, players[i].state.ent_m);
    snprintf(b+strlen(b), sizeof(b)-strlen(b), " NAV MODE: %s\n", (players[i].nav_state == NAV_STATE_CHASE) ? B_RED "[ CHASE ACTIVE ]" RESET : "[ NORMAL ]");
    strcat(b, BLUE "\n[ POWER AND REACTOR STATUS ]\n" RESET);
    float en_pct = (players[i].state.energy / 100000.0f) * 100.0f; char en_bar[21]; int en_fills = (int)(en_pct / 5); for(int j=0; j<20; j++) en_bar[j] = (j < en_fills) ? '|' : '-'; en_bar[20] = '\0';
    snprintf(b+strlen(b), sizeof(b)-strlen(b), " MAIN REACTOR: [%s] %d / 100000 (%.1f%%)\n ALLOCATION:   ENGINES: %.0f%%  SHIELDS: %.0f%%  WEAPONS: %.0f%%\n", en_bar, players[i].state.energy, en_pct, players[i].state.power_dist[0]*100, players[i].state.power_dist[1]*100, players[i].state.power_dist[2]*100);
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
    for(int j=1;j<=6;j++){ sprintf(it," %-12s: %-4d\n",r[j],players[i].state.inventory[j]); strcat(b,it); }
    snprintf(it, sizeof(it), BLUE " Stored Energy: %d\n Stored Torps:  %d\n" RESET, players[i].state.cargo_energy, players[i].state.cargo_torpedoes); strcat(b, it);
    send_server_msg(i, "LOGISTICS", b);
}

void handle_dam(int i, const char *params) {
    char b[512]=RED "\n--- DAMAGE REPORT ---\n" RESET; char sbuf[64]; const char* sys[]={"Warp","Impulse","Sensors","Transp","Phasers","Torps","Computer","Life"};
    for(int s=0;s<8;s++){ sprintf(sbuf," %-10s: %.1f%%\n",sys[s],players[i].state.system_health[s]); strcat(b,sbuf); }
    send_server_msg(i, "ENGINEERING", b);
}

void handle_cal(int i, const char *params) {
    int qx,qy,qz; 
    if(sscanf(params,"%d %d %d",&qx,&qy,&qz)==3) {
        double dx=(qx-players[i].state.q1)*10.0;
        double dy=(qy-players[i].state.q2)*10.0;
        double dz=(qz-players[i].state.q3)*10.0;
        double d=sqrt(dx*dx+dy*dy+dz*dz);
        
        double h=0, m=0;
        if (d > 0.001) {
            h=atan2(dx,-dy)*180.0/M_PI; if(h<0)h+=360.0;
            m=asin(dz/d)*180.0/M_PI;
        }
        
        char buf[128]; 
        if (d < 0.001) {
            sprintf(buf, "Navigation: Ship is already at Q[%d,%d,%d].", qx, qy, qz);
        } else {
            sprintf(buf,"Course to Q[%d,%d,%d]: H:%.1f M:%.1f W:%.2f", qx,qy,qz,h,m,d/10.0);
        }
        send_server_msg(i,"COMPUTER",buf);
    }
}

void handle_who(int i, const char *params) {
    char b[4096] = WHITE "\n--- ACTIVE CAPTAINS IN GALAXY ---\n" RESET;
    for(int j=0; j<MAX_CLIENTS; j++) if(players[j].active) {
        char line[256]; sprintf(line, " ID:%-2d  %-16s  LOC:[%d,%d,%d]  STATUS:%s\n", j+1, players[j].name, players[j].state.q1, players[j].state.q2, players[j].state.q3, players[j].state.is_cloaked ? MAGENTA "CLOAKED" RESET : GREEN "ONLINE" RESET);
        strcat(b, line);
    }
    send_server_msg(i, "COMPUTER", b);
}

void handle_aux(int i, const char *params) {
    if (strncmp(params, "probe ", 6) == 0) {
        int qx,qy,qz; if(sscanf(params+6, "%d %d %d", &qx,&qy,&qz)==3 && IS_Q_VALID(qx,qy,qz)) {
            int v=galaxy_master.g[qx][qy][qz]; char buf[256]; sprintf(buf, "Probe Q[%d,%d,%d]: %05d (BH:%d P:%d E:%d B:%d S:%d)", qx,qy,qz,v,(v/10000)%10,(v/1000)%10,(v/100)%10,(v/10)%10,v%10);
            send_server_msg(i, "SCIENCE", buf);
        }
    } else if (strncmp(params, "computer", 8) == 0) {
        char buf[512]; sprintf(buf, "\n--- STRATEGIC ANALYSIS ---\nHostiles: %d\nBases: %d\nStability: %.1f%%", galaxy_master.k9, galaxy_master.b9, (1.0-(float)galaxy_master.k9/200.0)*100.0);
        send_server_msg(i, "COMPUTER", buf);
    } else if (strncmp(params, "jettison", 8) == 0) {
        send_server_msg(i, "ENGINEERING", "CORE JETTISONED!"); players[i].state.boom=(NetPoint){(float)players[i].state.s1,(float)players[i].state.s2,(float)players[i].state.s3,1}; players[i].active=0;
    }
}

/* --- Command Registry Table --- */

static const CommandDef command_registry[] = {
    {"nav ", handle_nav, "Warp Navigation"},
    {"imp ", handle_imp, "Impulse Drive"},
    {"apr ", handle_apr, "Approach target"},
    {"cha",  handle_cha, "Chase locked target"},
    {"srs",  handle_srs, "Short Range Sensors"},
    {"lrs",  handle_lrs, "Long Range Sensors"},
    {"pha ", handle_pha, "Fire Phasers"},
    {"tor",  handle_tor, "Fire Torpedo"},
    {"she ", handle_she, "Shield Configuration"},
    {"lock ",handle_lock, "Target Lock-on"},
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
    {"cal ", handle_cal, "Navigation Calculator"},
    {"who",  handle_who, "Active Captains List"},
    {"aux ", handle_aux, "Auxiliary Systems"},
    {NULL, NULL, NULL}
};

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