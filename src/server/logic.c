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
#include <stddef.h>
#include <sys/socket.h>
#include "server_internal.h"

/* Helper to safely calculate quadrant from absolute coordinate */
int get_q_from_g(double g) {
    int q = (int)(g / 10.0) + 1;
    if (q < 1) q = 1;
    if (q > 10) q = 10;
    return q;
}

/* --- Modular AI Controller --- */

void update_npc_ai(int n) {
    if (!npcs[n].active) return;

    /* Sync absolute if first time */
    if (npcs[n].gx <= 0.001 && npcs[n].gy <= 0.001) {
        npcs[n].gx = (npcs[n].q1-1)*10.0 + npcs[n].x;
        npcs[n].gy = (npcs[n].q2-1)*10.0 + npcs[n].y;
        npcs[n].gz = (npcs[n].q3-1)*10.0 + npcs[n].z;
    }

    int q1 = npcs[n].q1, q2 = npcs[n].q2, q3 = npcs[n].q3;
    if (!IS_Q_VALID(q1, q2, q3)) return;
    QuadrantIndex *local_q = &spatial_index[q1][q2][q3];
    
    int closest_p = -1; double min_d2 = 100.0;
    for (int j = 0; j < local_q->player_count; j++) {
        ConnectedPlayer *p = local_q->players[j]; if (p->state.is_cloaked) continue;
        double d2 = pow(npcs[n].gx - p->gx, 2) + pow(npcs[n].gy - p->gy, 2) + pow(npcs[n].gz - p->gz, 2);
        if (d2 < min_d2) { min_d2 = d2; closest_p = (int)(p - players); }
    }
    
    /* State Machine Logic */
    if (npcs[n].energy < 200) npcs[n].ai_state = AI_STATE_FLEE;
    else if (closest_p != -1) npcs[n].ai_state = AI_STATE_CHASE;
    else npcs[n].ai_state = AI_STATE_PATROL;

    double d_dx = 0, d_dy = 0, d_dz = 0, speed = 0.03;
    if (npcs[n].engine_health < 10.0f) speed = 0; else speed *= (npcs[n].engine_health/100.0f);

    if (npcs[n].ai_state == AI_STATE_CHASE && closest_p != -1) {
        /* Predictive Trajectory Tracking */
        double dx = players[closest_p].gx - npcs[n].gx, dy = players[closest_p].gy - npcs[n].gy, dz = players[closest_p].gz - npcs[n].gz;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        if (d > 2.1) { d_dx = dx/d; d_dy = dy/d; d_dz = dz/d; }
        
        /* Tactical Firing (Predictive) */
        if (npcs[n].fire_cooldown > 0) npcs[n].fire_cooldown--;
        if (npcs[n].fire_cooldown <= 0 && d < 6.0) {
            /* Beam FX logic mapped to player state for transmission */
            players[closest_p].state.beam_count = 1; 
            players[closest_p].state.beams[0] = (NetBeam){(float)npcs[n].x, (float)npcs[n].y, (float)npcs[n].z, 1};
            
            /* Damage Calculation */
            int dmg = 100;
            if (npcs[n].faction == FACTION_BORG) dmg = 500;
            else if (npcs[n].faction == FACTION_KLINGON) dmg = 250;
            
            players[closest_p].state.energy -= dmg;
            npcs[n].fire_cooldown = (npcs[n].faction == FACTION_BORG) ? 100 : 150;
        }
    } else if (npcs[n].ai_state == AI_STATE_FLEE && closest_p != -1) {
        double dx = npcs[n].gx - players[closest_p].gx, dy = npcs[n].gy - players[closest_p].gy, dz = npcs[n].gz - players[closest_p].gz;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        if (d > 0.1) { d_dx = dx/d; d_dy = dy/d; d_dz = dz/d; speed *= 1.8; }
        if (d > 8.5) npcs[n].ai_state = AI_STATE_PATROL; /* Safely away */
    } else {
        if (npcs[n].nav_timer-- <= 0) { 
            npcs[n].nav_timer = 100 + rand()%200; 
            double rx = (rand()%100-50)/100.0, ry = (rand()%100-50)/100.0, rz = (rand()%100-50)/100.0;
            double rl = sqrt(rx*rx + ry*ry + rz*rz);
            if (rl > 0.001) { npcs[n].dx = rx/rl; npcs[n].dy = ry/rl; npcs[n].dz = rz/rl; }
        }
        d_dx = npcs[n].dx; d_dy = npcs[n].dy; d_dz = npcs[n].dz;
    }
    
    /* Movement and Collision with Celestial Bodies */
    npcs[n].gx += d_dx * speed; npcs[n].gy += d_dy * speed; npcs[n].gz += d_dz * speed;
    
    /* Clamp to galaxy bounds */
    if (npcs[n].gx < 0.05) { npcs[n].gx = 0.05; }
    if (npcs[n].gx > 99.95) { npcs[n].gx = 99.95; }
    if (npcs[n].gy < 0.05) { npcs[n].gy = 0.05; }
    if (npcs[n].gy > 99.95) { npcs[n].gy = 99.95; }
    if (npcs[n].gz < 0.05) { npcs[n].gz = 0.05; }
    if (npcs[n].gz > 99.95) { npcs[n].gz = 99.95; }

    npcs[n].q1 = get_q_from_g(npcs[n].gx); npcs[n].q2 = get_q_from_g(npcs[n].gy); npcs[n].q3 = get_q_from_g(npcs[n].gz);
    npcs[n].x = npcs[n].gx - (npcs[n].q1 - 1) * 10.0; npcs[n].y = npcs[n].gy - (npcs[n].q2 - 1) * 10.0; npcs[n].z = npcs[n].gz - (npcs[n].q3 - 1) * 10.0;

    /* Enviromental Hazards */
    for(int h=0; h<local_q->bh_count; h++) {
        double d=sqrt(pow(local_q->black_holes[h]->x-npcs[n].x,2)+pow(local_q->black_holes[h]->y-npcs[n].y,2)+pow(local_q->black_holes[h]->z-npcs[n].z,2));
        if(d < 1.0) npcs[n].active = 0;
    }
}

void update_game_logic() {
    static int global_tick = 0;
    global_tick++;

    pthread_mutex_lock(&game_mutex);
    
    /* Phase 1: NPC Movement & AI */
    for (int n = 0; n < MAX_NPC; n++) update_npc_ai(n);

    /* Phase 2: Player Movement & Physics */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!players[i].active) continue;
        
        /* Crew Management Logic */
        if (global_tick % 100 == 0) {
            float ls_health = players[i].state.system_health[7]; /* Life Support */
            if (ls_health < 75.0f) {
                int loss = (ls_health < 25.0f) ? 5 : 1;
                players[i].state.crew_count -= loss;
                if (players[i].state.crew_count < 0) players[i].state.crew_count = 0;
                if (players[i].state.crew_count == 0) {
                    send_server_msg(i, "CRITICAL", "Life support failure. Crew lost. Vessel adrift.");
                    players[i].active = 0;
                    players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1};
                } else if (global_tick % 300 == 0) {
                    send_server_msg(i, "MEDICAL", "Warning: Casualties reported due to life support instability.");
                }
            }
        }

        /* Random Environmental Events */
        if (global_tick % 2000 == 0 && (rand() % 100 < 5)) {
            int event_type = rand() % 2;
            if (event_type == 0) {
                send_server_msg(i, "SCIENCE", "Ion Storm detected! Sensors effectively blinded.");
                players[i].state.system_health[2] *= 0.5f; /* Damage sensors */
            } else {
                send_server_msg(i, "ENGINEERING", "Subspace surge detected. Power levels fluctuating.");
                players[i].state.energy += (rand() % 10000) - 5000;
                if (players[i].state.energy < 0) players[i].state.energy = 0;
            }
        }

        if (players[i].gx <= 0.001 && players[i].gy <= 0.001) {
            players[i].gx = (players[i].state.q1-1)*10.0 + players[i].state.s1;
            players[i].gy = (players[i].state.q2-1)*10.0 + players[i].state.s2;
            players[i].gz = (players[i].state.q3-1)*10.0 + players[i].state.s3;
        }

        if (players[i].nav_state == NAV_STATE_ALIGN) {
            players[i].nav_timer--;
            double t = 1.0 - (double)players[i].nav_timer / 60.0;
            players[i].state.ent_h = players[i].start_h + (players[i].target_h - players[i].start_h) * t;
            players[i].state.ent_m = players[i].start_m + (players[i].target_m - players[i].start_m) * t;
            if (players[i].nav_timer <= 0) {
                players[i].nav_state = NAV_STATE_WARP;
                double dist = sqrt(pow(players[i].target_gx - players[i].gx, 2) + pow(players[i].target_gy - players[i].gy, 2) + pow(players[i].target_gz - players[i].gz, 2));
                players[i].nav_timer = (int)(dist / 10.0 * 90.0); if (players[i].nav_timer < 30) players[i].nav_timer = 30;
                players[i].warp_speed = dist / players[i].nav_timer;
            }
        }
        else if (players[i].nav_state == NAV_STATE_ALIGN_IMPULSE) {
            players[i].nav_timer--;
            double t = 1.0 - (double)players[i].nav_timer / 60.0;
            players[i].state.ent_h = players[i].start_h + (players[i].target_h - players[i].start_h) * t;
            players[i].state.ent_m = players[i].start_m + (players[i].target_m - players[i].start_m) * t;
            if (players[i].nav_timer <= 0) {
                players[i].nav_state = NAV_STATE_IMPULSE;
                char msg[64]; sprintf(msg, "Impulse engaged at %.0f%%.", players[i].warp_speed * 200.0);
                send_server_msg(i, "HELMSMAN", msg);
            }
        }
        else if (players[i].nav_state == NAV_STATE_WARP) {
            players[i].nav_timer--;
            players[i].gx += players[i].dx * players[i].warp_speed;
            players[i].gy += players[i].dy * players[i].warp_speed;
            players[i].gz += players[i].dz * players[i].warp_speed;
            if (players[i].nav_timer <= 0) { players[i].nav_state = NAV_STATE_REALIGN; players[i].nav_timer = 60; players[i].start_h = players[i].state.ent_h; players[i].start_m = players[i].state.ent_m; }
        }
        else if (players[i].nav_state == NAV_STATE_REALIGN) {
            players[i].nav_timer--;
            double t = 1.0 - (double)players[i].nav_timer / 60.0;
            players[i].state.ent_m = players[i].start_m * (1.0 - t);
            if (players[i].nav_timer <= 0) { 
                players[i].state.ent_m = 0; 
                players[i].nav_state = NAV_STATE_IDLE; 
                send_server_msg(i, "HELMSMAN", "Stabilization complete. Ship aligned.");
            }
        }
        else if (players[i].nav_state == NAV_STATE_IMPULSE) {
            if (players[i].state.energy > 0) {
                players[i].state.energy -= 1;
                players[i].gx += players[i].dx * players[i].warp_speed;
                players[i].gy += players[i].dy * players[i].warp_speed;
                players[i].gz += players[i].dz * players[i].warp_speed;
            }
        }
        else if (players[i].nav_state == NAV_STATE_CHASE) {
            int tid = players[i].state.lock_target;
            double tx, ty, tz, tvx=0, tvy=0, tvz=0; bool found = false;
            int tq1=0, tq2=0, tq3=0;

            if (tid >= 1 && tid <= 32 && players[tid-1].active) {
                tx = players[tid-1].gx; ty = players[tid-1].gy; tz = players[tid-1].gz;
                tvx = players[tid-1].dx * players[tid-1].warp_speed; tvy = players[tid-1].dy * players[tid-1].warp_speed; tvz = players[tid-1].dz * players[tid-1].warp_speed;
                tq1 = players[tid-1].state.q1; tq2 = players[tid-1].state.q2; tq3 = players[tid-1].state.q3;
                found = true;
            } else if (tid >= 100 && tid < 100+MAX_NPC && npcs[tid-100].active) {
                tx = npcs[tid-100].gx; ty = npcs[tid-100].gy; tz = npcs[tid-100].gz;
                tvx = npcs[tid-100].dx * 0.03; tvy = npcs[tid-100].dy * 0.03; tvz = npcs[tid-100].dz * 0.03;
                tq1 = npcs[tid-100].q1; tq2 = npcs[tid-100].q2; tq3 = npcs[tid-100].q3;
                found = true;
            }

            if (found && players[i].state.energy > 5000) {
                double dx = tx - players[i].gx, dy = ty - players[i].gy, dz = tz - players[i].gz;
                double dist = sqrt(dx*dx + dy*dy + dz*dz);
                
                /* Subspace Tracking: Calculate vectors using galactic coordinates */
                if (dist > 0.05) {
                    double des_h = atan2(dx, -dy) * 180.0 / M_PI; if(des_h<0) des_h+=360;
                    double des_m = asin(dz/dist) * 180.0 / M_PI;
                    double diff_h = des_h - players[i].state.ent_h;
                    while (diff_h > 180) { diff_h -= 360; }
                    while (diff_h < -180) { diff_h += 360; }
                    players[i].state.ent_h += diff_h * 0.15;
                    players[i].state.ent_m += (des_m - players[i].state.ent_m) * 0.15;
                    if (players[i].state.ent_h >= 360) { players[i].state.ent_h -= 360; }
                    if (players[i].state.ent_h < 0) { players[i].state.ent_h += 360; }
                }
                
                double rad_h = players[i].state.ent_h * M_PI / 180.0;
                double rad_m = players[i].state.ent_m * M_PI / 180.0;
                players[i].dx = cos(rad_m) * sin(rad_h); players[i].dy = cos(rad_m) * -cos(rad_h); players[i].dz = sin(rad_m);
                
                /* Auto-Warp: Increase speed if target is in another quadrant (dist > 10) */
                double target_dist = (players[i].approach_dist > 0.05) ? players[i].approach_dist : 2.0;
                double base_speed = (dist > 10.0) ? 0.8 : 0.4;
                double ideal_speed = (dist - target_dist) * base_speed + sqrt(tvx*tvx + tvy*tvy + tvz*tvz);
                
                if (ideal_speed > 0.8) { ideal_speed = 0.8; }
                if (ideal_speed < -0.1) { ideal_speed = -0.1; }
                
                players[i].warp_speed = (players[i].warp_speed * 0.7) + (ideal_speed * 0.3);
                players[i].gx += players[i].dx * players[i].warp_speed;
                players[i].gy += players[i].dy * players[i].warp_speed;
                players[i].gz += players[i].dz * players[i].warp_speed;
                
                int drain = 10 + (int)(fabs(players[i].warp_speed)*20.0);
                players[i].state.energy -= drain;
                
                /* Quadrant Transition Check */
                if (players[i].state.q1 != tq1 || players[i].state.q2 != tq2 || players[i].state.q3 != tq3) {
                    static int last_warn = 0;
                    if (global_tick - last_warn > 300) {
                        send_server_msg(i, "HELMSMAN", "Target has left the quadrant. Engaging inter-sector subspace tracking.");
                        last_warn = global_tick;
                    }
                }
            } else {
                players[i].nav_state = NAV_STATE_IDLE;
                if (!found) send_server_msg(i, "COMPUTER", "Chase target lost.");
            }
        }

        /* Galactic Barrier Enforcement: Standardized for all axes and corners */
        bool hit_barrier = false;
        if (players[i].gx < 0.05) { players[i].gx = 0.05; hit_barrier = true; }
        else if (players[i].gx > 99.95) { players[i].gx = 99.95; hit_barrier = true; }
        
        if (players[i].gy < 0.05) { players[i].gy = 0.05; hit_barrier = true; }
        else if (players[i].gy > 99.95) { players[i].gy = 99.95; hit_barrier = true; }
        
        if (players[i].gz < 0.05) { players[i].gz = 0.05; hit_barrier = true; }
        else if (players[i].gz > 99.95) { players[i].gz = 99.95; hit_barrier = true; }

        if (hit_barrier && players[i].nav_state != NAV_STATE_CHASE && players[i].nav_state != NAV_STATE_IDLE) {
            players[i].nav_state = NAV_STATE_IDLE;
            players[i].warp_speed = 0;
            send_server_msg(i, "HELMSMAN", "Warning: We have hit the Galactic Barrier. Engines disengaged.");
        }

        /* Update Sector Coordinates and Quadrant */
        players[i].state.q1 = get_q_from_g(players[i].gx);
        players[i].state.q2 = get_q_from_g(players[i].gy);
        players[i].state.q3 = get_q_from_g(players[i].gz);
        players[i].state.s1 = players[i].gx - (players[i].state.q1 - 1) * 10.0;
        players[i].state.s2 = players[i].gy - (players[i].state.q2 - 1) * 10.0;
        players[i].state.s3 = players[i].gz - (players[i].state.q3 - 1) * 10.0;

        /* Collision Detection: Celestial Bodies */
        QuadrantIndex *current_q = &spatial_index[players[i].state.q1][players[i].state.q2][players[i].state.q3];
        for (int s = 0; s < current_q->star_count; s++) {
            double d = sqrt(pow(players[i].state.s1 - current_q->stars[s]->x, 2) + pow(players[i].state.s2 - current_q->stars[s]->y, 2) + pow(players[i].state.s3 - current_q->stars[s]->z, 2));
            if (d < 0.8) { send_server_msg(i, "CRITICAL", "Impact with star corona! Hull melting..."); players[i].active = 0; players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1}; break; }
        }
        if (players[i].active) for (int p = 0; p < current_q->planet_count; p++) {
            double d = sqrt(pow(players[i].state.s1 - current_q->planets[p]->x, 2) + pow(players[i].state.s2 - current_q->planets[p]->y, 2) + pow(players[i].state.s3 - current_q->planets[p]->z, 2));
            if (d < 0.8) { send_server_msg(i, "CRITICAL", "Planetary collision! Structural failure."); players[i].active = 0; players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1}; break; }
        }

        /* Target Lock Validation (Inter-Quadrant aware) */
        if (players[i].state.lock_target > 0) {
            int tid = players[i].state.lock_target;
            bool valid = false;
            int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
            
            if (tid >= 1 && tid <= 32) {
                /* Players can be locked as long as they are active anywhere */
                if (players[tid-1].active) valid = true;
            } else if (tid >= 100 && tid < 100+MAX_NPC) {
                if (npcs[tid-100].active) valid = true;
            } else if (tid >= 500 && tid < 500+MAX_BASES) {
                /* Static objects remain local-only for locking sanity */
                if (bases[tid-500].active && bases[tid-500].q1 == pq1 && bases[tid-500].q2 == pq2 && bases[tid-500].q3 == pq3) valid = true;
            } else if (tid >= 1000 && tid < 1000+MAX_PLANETS) {
                if (planets[tid-1000].active && planets[tid-1000].q1 == pq1 && planets[tid-1000].q2 == pq2 && planets[tid-1000].q3 == pq3) valid = true;
            } else if (tid >= 2000 && tid < 2000+MAX_STARS) {
                if (stars_data[tid-2000].active && stars_data[tid-2000].q1 == pq1 && stars_data[tid-2000].q2 == pq2 && stars_data[tid-2000].q3 == pq3) valid = true;
            } else if (tid >= 3000 && tid < 3000+MAX_BH) {
                if (black_holes[tid-3000].active && black_holes[tid-3000].q1 == pq1 && black_holes[tid-3000].q2 == pq2 && black_holes[tid-3000].q3 == pq3) valid = true;
            }

            if (!valid) {
                players[i].state.lock_target = 0;
                send_server_msg(i, "TACTICAL", "Target lost. Lock released.");
            }
        }

        /* Torpedo Physics */
        if (players[i].torp_active) {
            /* Guidance System */
            if (players[i].torp_target > 0) {
                double target_x = -1, target_y = -1, target_z = -1;
                int tid = players[i].torp_target;
                int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
                if (tid <= 32 && players[tid-1].active && players[tid-1].state.q1 == pq1 && players[tid-1].state.q2 == pq2 && players[tid-1].state.q3 == pq3) {
                    target_x = players[tid-1].state.s1; target_y = players[tid-1].state.s2; target_z = players[tid-1].state.s3;
                } else if (tid >= 100 && tid < 100+MAX_NPC && npcs[tid-100].active && npcs[tid-100].q1 == pq1 && npcs[tid-100].q2 == pq2 && npcs[tid-100].q3 == pq3) {
                    target_x = npcs[tid-100].x; target_y = npcs[tid-100].y; target_z = npcs[tid-100].z;
                }
                if (target_x != -1) {
                    double dx = target_x - players[i].tx, dy = target_y - players[i].ty, dz = target_z - players[i].tz;
                    double d = sqrt(dx*dx + dy*dy + dz*dz);
                    if (d > 0.01) {
                        players[i].tdx = (players[i].tdx * 0.8) + ((dx/d) * 0.2);
                        players[i].tdy = (players[i].tdy * 0.8) + ((dy/d) * 0.2);
                        players[i].tdz = (players[i].tdz * 0.8) + ((dz/d) * 0.2);
                        double s = sqrt(players[i].tdx*players[i].tdx + players[i].tdy*players[i].tdy + players[i].tdz*players[i].tdz);
                        players[i].tdx /= s; players[i].tdy /= s; players[i].tdz /= s;
                    }
                }
            }
            players[i].tx += players[i].tdx * 0.8; players[i].ty += players[i].tdy * 0.8; players[i].tz += players[i].tdz * 0.8;
            players[i].state.torp = (NetPoint){(float)players[i].tx, (float)players[i].ty, (float)players[i].tz, 1};
            
            /* Collision Detection */
            bool hit = false;
            QuadrantIndex *lq = &spatial_index[players[i].state.q1][players[i].state.q2][players[i].state.q3];
            for (int j=0; j<lq->player_count; j++) {
                ConnectedPlayer *p = lq->players[j]; if (p == &players[i] || !p->active) continue;
                double d = sqrt(pow(players[i].tx - p->state.s1, 2) + pow(players[i].ty - p->state.s2, 2) + pow(players[i].tz - p->state.s3, 2));
                if (d < 0.5) {
                    int dmg = 75000;
                    for(int s=0;s<6;s++) { int abs=(p->state.shields[s]>=dmg/6)?dmg/6:p->state.shields[s]; p->state.shields[s]-=abs; dmg-=abs; }
                    p->state.energy -= dmg; send_server_msg((int)(p-players), "WARNING", "HIT BY PHOTON TORPEDO!");
                    if(p->state.energy <= 0) { p->active = 0; p->state.boom = (NetPoint){(float)p->state.s1, (float)p->state.s2, (float)p->state.s3, 1}; }
                    hit = true; break;
                }
            }
            if (!hit) for (int n=0; n<lq->npc_count; n++) {
                NPCShip *npc = lq->npcs[n];
                double d = sqrt(pow(players[i].tx - npc->x, 2) + pow(players[i].ty - npc->y, 2) + pow(players[i].tz - npc->z, 2));
                if (d < 0.5) { npc->energy -= 75000; if(npc->energy <= 0) { npc->active = 0; players[i].state.boom = (NetPoint){(float)npc->x, (float)npc->y, (float)npc->z, 1}; } hit = true; break; }
            }
            if (hit || players[i].tx<0||players[i].tx>10||players[i].ty<0||players[i].ty>10||players[i].tz<0||players[i].tz>10) {
                if (hit) { players[i].state.boom = (NetPoint){(float)players[i].tx, (float)players[i].ty, (float)players[i].tz, 1}; send_server_msg(i, "TACTICAL", "Torpedo impact confirmed."); }
                players[i].torp_active = false; players[i].state.torp.active = 0;
            }
        }
    }

    rebuild_spatial_index();
    if (global_tick % 1800 == 0) save_galaxy();
    pthread_mutex_unlock(&game_mutex);

    /* Phase 3: Network Updates */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!players[i].active || players[i].socket == 0) continue;
        pthread_mutex_lock(&game_mutex);
        PacketUpdate upd; memset(&upd, 0, sizeof(PacketUpdate)); upd.type = PKT_UPDATE;
        upd.q1 = players[i].state.q1; upd.q2 = players[i].state.q2; upd.q3 = players[i].state.q3;
        upd.s1 = players[i].state.s1; upd.s2 = players[i].state.s2; upd.s3 = players[i].state.s3;
        upd.ent_h = players[i].state.ent_h; upd.ent_m = players[i].state.ent_m;
        upd.energy = players[i].state.energy; upd.torpedoes = players[i].state.torpedoes;
        upd.cargo_energy = players[i].state.cargo_energy; upd.cargo_torpedoes = players[i].state.cargo_torpedoes;
        upd.crew_count = players[i].state.crew_count;
        for(int s=0; s<6; s++) upd.shields[s] = players[i].state.shields[s];
        upd.is_cloaked = players[i].state.is_cloaked;
        int o_idx = 0;
        upd.objects[o_idx] = (NetObject){(float)players[i].state.s1,(float)players[i].state.s2,(float)players[i].state.s3,(float)players[i].state.ent_h,(float)players[i].state.ent_m,1,players[i].ship_class,1,(int)((players[i].state.energy/100000.0)*100),i+1,""};
        strncpy(upd.objects[o_idx++].name, players[i].name, 63);
        if (IS_Q_VALID(upd.q1, upd.q2, upd.q3)) {
            QuadrantIndex *lq = &spatial_index[upd.q1][upd.q2][upd.q3];
            for(int j=0; j<lq->player_count; j++) {
                ConnectedPlayer *p = lq->players[j]; if (p == &players[i] || !p->active || o_idx >= MAX_NET_OBJECTS) continue;
                upd.objects[o_idx] = (NetObject){(float)p->state.s1,(float)p->state.s2,(float)p->state.s3,(float)p->state.ent_h,(float)p->state.ent_m,1,p->ship_class,1,(int)((p->state.energy/100000.0)*100),(int)(p-players)+1,""};
                strncpy(upd.objects[o_idx++].name, p->name, 63);
            }
            for(int n=0; n<lq->npc_count && o_idx < MAX_NET_OBJECTS; n++) {
                NPCShip *npc = lq->npcs[n]; int max_e = (npc->faction==FACTION_BORG)?100000:50000;
                upd.objects[o_idx] = (NetObject){(float)npc->x,(float)npc->y,(float)npc->z,0,0,npc->faction,0,1,(int)((npc->energy/(float)max_e)*100),npc->id+100,""};
                strncpy(upd.objects[o_idx++].name, get_species_name(npc->faction), 63);
            }
            for(int p=0; p<lq->planet_count && o_idx < MAX_NET_OBJECTS; p++) upd.objects[o_idx++] = (NetObject){(float)lq->planets[p]->x,(float)lq->planets[p]->y,(float)lq->planets[p]->z,0,0,5,0,1,100,lq->planets[p]->id+1000,"Planet"};
            for(int s=0; s<lq->star_count && o_idx < MAX_NET_OBJECTS; s++) upd.objects[o_idx++] = (NetObject){(float)lq->stars[s]->x,(float)lq->stars[s]->y,(float)lq->stars[s]->z,0,0,4,0,1,100,lq->stars[s]->id+2000,"Star"};
            for(int h=0; h<lq->bh_count && o_idx < MAX_NET_OBJECTS; h++) upd.objects[o_idx++] = (NetObject){(float)lq->black_holes[h]->x,(float)lq->black_holes[h]->y,(float)lq->black_holes[h]->z,0,0,6,0,1,100,lq->black_holes[h]->id+3000,"Black Hole"};
            for(int b=0; b<lq->base_count && o_idx < MAX_NET_OBJECTS; b++) upd.objects[o_idx++] = (NetObject){(float)lq->bases[b]->x,(float)lq->bases[b]->y,(float)lq->bases[b]->z,0,0,3,0,1,100,lq->bases[b]->id+500,"Starbase"};
        }
        upd.object_count = o_idx;
        upd.beam_count = players[i].state.beam_count; for(int b=0; b<upd.beam_count && b<MAX_NET_BEAMS; b++) upd.beams[b] = players[i].state.beams[b];
        upd.torp = players[i].state.torp; upd.boom = players[i].state.boom; upd.dismantle = players[i].state.dismantle;
        players[i].state.beam_count = 0; players[i].state.boom.active = 0; players[i].state.dismantle.active = 0;
        int current_sock = players[i].socket;
        pthread_mutex_unlock(&game_mutex);
        if (current_sock != 0) { size_t p_size = sizeof(PacketUpdate) - sizeof(NetObject) * (MAX_NET_OBJECTS - upd.object_count); if (p_size < offsetof(PacketUpdate, objects)) p_size = offsetof(PacketUpdate, objects); write_all(current_sock, &upd, p_size); }
    }
}