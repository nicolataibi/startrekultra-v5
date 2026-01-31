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
            int dmg = 10;
            if (npcs[n].faction == FACTION_BORG) dmg = 50;
            else if (npcs[n].faction == FACTION_KLINGON) dmg = 25;
            
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
    global_tick++;

    pthread_mutex_lock(&game_mutex);
    
    /* Phase 0: Map cleanup (Storms) */
    if (global_tick % 500 == 0) {
        for(int i=1; i<=10; i++) for(int j=1; j<=10; j++) for(int l=1; l<=10; l++) {
            if (galaxy_master.g[i][j][l] >= 10000000) galaxy_master.g[i][j][l] -= 10000000;
        }
    }

    /* Phase 1: NPC Movement & AI */
    for (int n = 0; n < MAX_NPC; n++) update_npc_ai(n);

    /* Phase 1.2: Platform AI (Static Defense) */
    for (int pt = 0; pt < MAX_PLATFORMS; pt++) {
        if (!platforms[pt].active) continue;
        if (platforms[pt].fire_cooldown > 0) platforms[pt].fire_cooldown--;
        
        if (platforms[pt].fire_cooldown <= 0) {
            int q1 = platforms[pt].q1, q2 = platforms[pt].q2, q3 = platforms[pt].q3;
            QuadrantIndex *local_q = &spatial_index[q1][q2][q3];
            
            for (int j = 0; j < local_q->player_count; j++) {
                ConnectedPlayer *p = local_q->players[j];
                if (p->state.is_cloaked) continue;
                
                double dx = p->state.s1 - platforms[pt].x;
                double dy = p->state.s2 - platforms[pt].y;
                double dz = p->state.s3 - platforms[pt].z;
                double dist = sqrt(dx*dx + dy*dy + dz*dz);
                
                if (dist < 5.0) {
                    /* Fire! */
                    p->state.beam_count = 1;
                    p->state.beams[0] = (NetBeam){(float)platforms[pt].x, (float)platforms[pt].y, (float)platforms[pt].z, 1};
                    p->state.energy -= 100; /* Heavy damage */
                    platforms[pt].fire_cooldown = 100; /* ~3.3 seconds */
                    break;
                }
            }
        }
    }

    /* Phase 1.5: Comet Orbital Movement */
    for (int c = 0; c < MAX_COMETS; c++) {
        if (!comets[c].active) continue;
        
        /* 1. Update orbital angle */
        comets[c].angle += comets[c].speed;
        if (comets[c].angle > 2*M_PI) comets[c].angle -= 2*M_PI;
        
        /* 2. Calculate position in orbital plane */
        double ox = comets[c].a * cos(comets[c].angle);
        double oy = comets[c].b * sin(comets[c].angle);
        
        /* 3. Rotate by inclination (simplified rotation around X-axis for variety) */
        double gx = comets[c].cx + ox;
        double gy = comets[c].cy + oy * cos(comets[c].inc);
        double gz = comets[c].cz + oy * sin(comets[c].inc);
        
        /* 4. Clamp to Galactic Bounds 0-100 */
        if (gx < 0) { gx = 0; } if (gx > 100) { gx = 100; }
        if (gy < 0) { gy = 0; } if (gy > 100) { gy = 100; }
        if (gz < 0) { gz = 0; } if (gz > 100) { gz = 100; }
        
        /* 5. Update local quadrant and sector */
        int nq1 = (int)(gx / 10.0) + 1; if(nq1>10) nq1=10; if(nq1<1) nq1=1;
        int nq2 = (int)(gy / 10.0) + 1; if(nq2>10) nq2=10; if(nq2<1) nq2=1;
        int nq3 = (int)(gz / 10.0) + 1; if(nq3>10) nq3=10; if(nq3<1) nq3=1;
        
        comets[c].q1 = nq1; comets[c].q2 = nq2; comets[c].q3 = nq3;
        comets[c].x = gx - (nq1-1)*10.0;
        comets[c].y = gy - (nq2-1)*10.0;
        comets[c].z = gz - (nq3-1)*10.0;
    }

    /* Phase 1.6: Supernova Event Logic */
    if (supernova_event.supernova_timer > 0) {
        supernova_event.supernova_timer--;
        
        int q1 = supernova_event.supernova_q1, q2 = supernova_event.supernova_q2, q3 = supernova_event.supernova_q3;
        /* Force negative value in galaxy grid to trigger red blinking on all client maps */
        galaxy_master.g[q1][q2][q3] = -supernova_event.supernova_timer;

        int sec = supernova_event.supernova_timer / 30;
        if (sec > 0 && (supernova_event.supernova_timer % 300 == 0 || (sec <= 10 && supernova_event.supernova_timer % 30 == 0))) {
            char msg[128];
            sprintf(msg, "!!! WARNING: SUPERNOVA IMMINENT IN Q-%d-%d-%d. T-MINUS %d SECONDS !!!", 
                    supernova_event.supernova_q1, supernova_event.supernova_q2, supernova_event.supernova_q3, sec);
            for(int i=0; i<MAX_CLIENTS; i++) if(players[i].active) send_server_msg(i, "SCIENCE", msg);
        }

        if (supernova_event.supernova_timer == 0) {
            /* KABOOM! Destroy everything in that quadrant */
            int q1 = supernova_event.supernova_q1, q2 = supernova_event.supernova_q2, q3 = supernova_event.supernova_q3;
            LOG_DEBUG("SUPERNOVA EXPLOSION in Q-%d-%d-%d\n", q1, q2, q3);

            /* Destroy Stars */
            for(int s=0; s<MAX_STARS; s++) if(stars_data[s].active && stars_data[s].q1 == q1 && stars_data[s].q2 == q2 && stars_data[s].q3 == q3) stars_data[s].active = 0;
            /* Destroy Planets */
            for(int p=0; p<MAX_PLANETS; p++) if(planets[p].active && planets[p].q1 == q1 && planets[p].q2 == q2 && planets[p].q3 == q3) planets[p].active = 0;
            /* Destroy NPCs */
            for(int n=0; n<MAX_NPC; n++) if(npcs[n].active && npcs[n].q1 == q1 && npcs[n].q2 == q2 && npcs[n].q3 == q3) npcs[n].active = 0;
            /* Destroy Bases */
            for(int b=0; b<MAX_BASES; b++) if(bases[b].active && bases[b].q1 == q1 && bases[b].q2 == q2 && bases[b].q3 == q3) bases[b].active = 0;
            
            /* Destroy Players */
            for(int i=0; i<MAX_CLIENTS; i++) {
                if(players[i].active && players[i].state.q1 == q1 && players[i].state.q2 == q2 && players[i].state.q3 == q3) {
                    send_server_msg(i, "CRITICAL", "SUPERNOVA IMPACT. VESSEL VAPORIZED.");
                    players[i].state.energy = 0; players[i].state.crew_count = 0;
                    players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1};
                    players[i].active = 0;
                }
            }

            /* Convert to a Black Hole remnant in the galaxy map */
            galaxy_master.g[q1][q2][q3] = 10000; /* BPNBS: 1 Black Hole, 0 Planets, 0 Bases, 0 Stars */
            
            /* Create the physical Black Hole object in the quadrant */
            for(int bh=0; bh<MAX_BH; bh++) {
                if(!black_holes[bh].active) {
                    black_holes[bh].id = bh;
                    black_holes[bh].q1 = q1; black_holes[bh].q2 = q2; black_holes[bh].q3 = q3;
                    black_holes[bh].x = supernova_event.x;
                    black_holes[bh].y = supernova_event.y;
                    black_holes[bh].z = supernova_event.z;
                    black_holes[bh].active = 1;
                    break;
                }
            }

            supernova_event.supernova_timer = 0; /* EXPLICITLY CLEAR EVENT */
            rebuild_spatial_index();
            save_galaxy();
        }
    } else {
        /* Small chance to trigger a new supernova if none active */
        if (global_tick > 100 && supernova_event.supernova_timer <= 0 && (rand() % 10000 < 5)) {
            int rq1 = rand()%10+1, rq2 = rand()%10+1, rq3 = rand()%10+1;
            QuadrantIndex *qi = &spatial_index[rq1][rq2][rq3];
            if (qi->star_count > 0) {
                supernova_event.supernova_q1 = rq1;
                supernova_event.supernova_q2 = rq2;
                supernova_event.supernova_q3 = rq3;
                supernova_event.supernova_timer = 1800;
                supernova_event.x = qi->stars[0]->x;
                supernova_event.y = qi->stars[0]->y;
                supernova_event.z = qi->stars[0]->z;
            }
        }
    }

    /* Phase 1.7: Monster AI Logic */

    /* Phase 1.7: Monster AI Logic */
    for (int mo = 0; mo < MAX_MONSTERS; mo++) {
        if (!monsters[mo].active) continue;
        int q1 = monsters[mo].q1, q2 = monsters[mo].q2, q3 = monsters[mo].q3;
        QuadrantIndex *local_q = &spatial_index[q1][q2][q3];
        
        ConnectedPlayer *target = NULL; double min_d = 10.0;
        for (int j = 0; j < local_q->player_count; j++) {
            ConnectedPlayer *p = local_q->players[j]; if (p->state.is_cloaked) continue;
            double dx = p->state.s1 - monsters[mo].x, dy = p->state.s2 - monsters[mo].y, dz = p->state.s3 - monsters[mo].z;
            double d = sqrt(dx*dx + dy*dy + dz*dz);
            if (d < min_d) { min_d = d; target = p; }
        }

        if (monsters[mo].type == 30) { /* Crystalline Entity */
            if (target) {
                double dx = target->state.s1 - monsters[mo].x, dy = target->state.s2 - monsters[mo].y, dz = target->state.s3 - monsters[mo].z;
                monsters[mo].x += (dx/min_d) * 0.05; monsters[mo].y += (dy/min_d) * 0.05; monsters[mo].z += (dz/min_d) * 0.05;
                if (min_d < 4.0 && global_tick % 60 == 0) {
                    target->state.beam_count = 1;
                    target->state.beams[0] = (NetBeam){(float)monsters[mo].x, (float)monsters[mo].y, (float)monsters[mo].z, 30};
                    target->state.energy -= 500;
                    send_server_msg((int)(target-players), "SCIENCE", "CRYSTALLINE RESONANCE DETECTED! SHIELDS BUCKLING!");
                }
            }
        } else if (monsters[mo].type == 31) { /* Space Amoeba */
            if (target && min_d < 1.5) {
                target->state.energy -= 200;
                if (global_tick % 30 == 0) send_server_msg((int)(target-players), "WARNING", "SPACE AMOEBA ADHERING TO HULL! ENERGY DRAIN CRITICAL!");
            }
        }
    }

    /* Phase 2: Player Interaction & Hazards */
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

        /* Random Environmental Events - Increased frequency */
        if (global_tick % 1000 == 0 && (rand() % 100 < 20)) {
            int event_type = rand() % 4; /* 0,1 = Ion Storm, 2 = Shear, 3 = Power Surge */
            if (event_type <= 1) {
                send_server_msg(i, "SCIENCE", "Ion Storm detected! Sensors effectively blinded.");
                players[i].state.system_health[2] *= 0.5f; /* Damage sensors */
                /* Mark storm in galaxy grid for the map (8th digit) */
                int q1=players[i].state.q1, q2=players[i].state.q2, q3=players[i].state.q3;
                if (IS_Q_VALID(q1,q2,q3)) {
                    if (galaxy_master.g[q1][q2][q3] < 10000000) galaxy_master.g[q1][q2][q3] += 10000000;
                }
            } else if (event_type == 1) {
                send_server_msg(i, "HELMSMAN", "Spatial shear encountered! We are being pushed off course!");
                players[i].gx += (rand()%100 - 50) / 50.0;
                players[i].gy += (rand()%100 - 50) / 50.0;
                players[i].gz += (rand()%100 - 50) / 50.0;
            } else {
                send_server_msg(i, "ENGINEERING", "Subspace surge detected. Power levels fluctuating.");
                players[i].state.energy += (rand() % 10000) - 5000;
                if (players[i].state.energy < 0) players[i].state.energy = 0;
            }
        }

        /* Anomaly Effects: Nebulas & Pulsars */
        QuadrantIndex *anomaly_q = &spatial_index[players[i].state.q1][players[i].state.q2][players[i].state.q3];
        
        for (int n = 0; n < anomaly_q->nebula_count; n++) {
            double d = sqrt(pow(players[i].state.s1 - anomaly_q->nebulas[n]->x, 2) + pow(players[i].state.s2 - anomaly_q->nebulas[n]->y, 2) + pow(players[i].state.s3 - anomaly_q->nebulas[n]->z, 2));
            if (d < 2.0) {
                 if (global_tick % 60 == 0) { /* Once per second */
                     players[i].state.energy -= 50;
                     if (players[i].state.energy < 0) players[i].state.energy = 0;
                 }
                 if (global_tick % 300 == 0) send_server_msg(i, "COMPUTER", "Alert: Nebular interference draining shields.");
            }
        }
        for (int p = 0; p < anomaly_q->pulsar_count; p++) {
            double d = sqrt(pow(players[i].state.s1 - anomaly_q->pulsars[p]->x, 2) + pow(players[i].state.s2 - anomaly_q->pulsars[p]->y, 2) + pow(players[i].state.s3 - anomaly_q->pulsars[p]->z, 2));
            if (d < 2.5) {
                if (global_tick % 60 == 0) {
                    int dmg = (int)((2.5 - d) * 400.0);
                    int shield_hit = 0;
                    for(int s=0; s<6; s++) { 
                        if(players[i].state.shields[s] > 0) {
                            int abs = (players[i].state.shields[s] >= dmg/6) ? dmg/6 : players[i].state.shields[s];
                            players[i].state.shields[s] -= abs;
                            shield_hit += abs;
                        }
                    }
                    if (shield_hit < dmg) {
                        /* Radiation penetrates shields */
                        players[i].state.crew_count -= (rand()%5 + 1);
                        if (players[i].state.crew_count < 0) players[i].state.crew_count = 0;
                    }
                    char msg[64]; sprintf(msg, "Radiation Critical! Shield Integrity Failing. (Dmg: %d)", dmg);
                    send_server_msg(i, "WARNING", msg);
                    
                    if (players[i].state.crew_count == 0) {
                         send_server_msg(i, "CRITICAL", "ALL HANDS LOST TO RADIATION.");
                         players[i].active = 0;
                         players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1};
                    }
                }
            }
        }

        /* Comet Interception Logic */
        for (int c = 0; c < anomaly_q->comet_count; c++) {
            double d = sqrt(pow(players[i].state.s1 - anomaly_q->comets[c]->x, 2) + pow(players[i].state.s2 - anomaly_q->comets[c]->y, 2) + pow(players[i].state.s3 - anomaly_q->comets[c]->z, 2));
            if (d < 0.6) {
                if (global_tick % 100 == 0) {
                    players[i].state.inventory[6] += 5; /* Gases */
                    send_server_msg(i, "ENGINEERING", "Collecting rare gases from comet tail.");
                }
            }
        }

        /* Asteroid Collision Logic */
        for (int a = 0; a < anomaly_q->asteroid_count; a++) {
            double d = sqrt(pow(players[i].state.s1 - anomaly_q->asteroids[a]->x, 2) + pow(players[i].state.s2 - anomaly_q->asteroids[a]->y, 2) + pow(players[i].state.s3 - anomaly_q->asteroids[a]->z, 2));
            if (d < 0.8) {
                if (players[i].warp_speed > 0.1) {
                    if (global_tick % 30 == 0) {
                        int dmg = (int)(players[i].warp_speed * 1000.0);
                        for(int s=0; s<6; s++) players[i].state.shields[s] -= (dmg/10);
                        players[i].state.system_health[1] -= 0.5f; /* Impulse engines damage */
                        send_server_msg(i, "WARNING", "Colliding with asteroids! Reduce speed!");
                    }
                }
            }
        }

        bool in_nebula = false;
        for (int n = 0; n < anomaly_q->nebula_count; n++) {
            double d = sqrt(pow(players[i].state.s1 - anomaly_q->nebulas[n]->x, 2) + pow(players[i].state.s2 - anomaly_q->nebulas[n]->y, 2) + pow(players[i].state.s3 - anomaly_q->nebulas[n]->z, 2));
            if (d < 2.0) { in_nebula = true; break; }
        }

        /* Nebula Shield Interference */
        if (in_nebula) {
            /* Shields recharge at 25% speed, and cloak is unstable */
            if (players[i].state.energy > 0) {
                for(int s=0; s<6; s++) if(players[i].state.shields[s] < 5000) players[i].state.shields[s] -= 2; /* Slow drain instead of recharge */
            }
        }

        /* Pulsar Radiation Logic */
        for (int p = 0; p < anomaly_q->pulsar_count; p++) {
            double d = sqrt(pow(players[i].state.s1 - anomaly_q->pulsars[p]->x, 2) + pow(players[i].state.s2 - anomaly_q->pulsars[p]->y, 2) + pow(players[i].state.s3 - anomaly_q->pulsars[p]->z, 2));
            if (d < 2.0) {
                /* Radiation penetrates shields */
                if (rand()%100 < 10) {
                    players[i].state.crew_count--;
                    send_server_msg(i, "MEDICAL", "RADIATION ALERT! EQUIPMENT FAILURE IN SICKBAY!");
                }
                players[i].state.energy -= 50; 
            }
        }

        /* Black Hole Gravity Pull */
        for (int h = 0; h < anomaly_q->bh_count; h++) {
            double dx = anomaly_q->black_holes[h]->x - players[i].state.s1;
            double dy = anomaly_q->black_holes[h]->y - players[i].state.s2;
            double dz = anomaly_q->black_holes[h]->z - players[i].state.s3;
            double d = sqrt(dx*dx + dy*dy + dz*dz);
            if (d < 3.0 && d > 0.1) {
                /* Pull player towards center */
                double force = 0.05 / (d * d);
                players[i].state.s1 += (dx/d) * force;
                players[i].state.s2 += (dy/d) * force;
                players[i].state.s3 += (dz/d) * force;
                /* Sync absolute coordinates */
                players[i].gx = (players[i].state.q1-1)*10.0 + players[i].state.s1;
                players[i].gy = (players[i].state.q2-1)*10.0 + players[i].state.s2;
                players[i].gz = (players[i].state.q3-1)*10.0 + players[i].state.s3;
            }
        }

        /* Mine Detonation Logic */
        for (int m = 0; m < anomaly_q->mine_count; m++) {
            if (!anomaly_q->mines[m]->active) continue;
            double d = sqrt(pow(players[i].state.s1 - anomaly_q->mines[m]->x, 2) + pow(players[i].state.s2 - anomaly_q->mines[m]->y, 2) + pow(players[i].state.s3 - anomaly_q->mines[m]->z, 2));
            if (d < 0.4) {
                /* BOOM! */
                anomaly_q->mines[m]->active = 0;
                players[i].state.boom = (NetPoint){(float)anomaly_q->mines[m]->x, (float)anomaly_q->mines[m]->y, (float)anomaly_q->mines[m]->z, 1};
                int dmg = 25000;
                for(int s=0; s<6; s++) { int abs=(players[i].state.shields[s]>=dmg/6)?dmg/6:players[i].state.shields[s]; players[i].state.shields[s]-=abs; dmg-=abs; }
                players[i].state.energy -= dmg;
                send_server_msg(i, "CRITICAL", "MINE DETONATION! PROXIMITY ALERT FAILURE!");
            }
        }

        /* Spatial Rift Teleportation Logic */
        for (int rf = 0; rf < anomaly_q->rift_count; rf++) {
            double d = sqrt(pow(players[i].state.s1 - anomaly_q->rifts[rf]->x, 2) + pow(players[i].state.s2 - anomaly_q->rifts[rf]->y, 2) + pow(players[i].state.s3 - anomaly_q->rifts[rf]->z, 2));
            if (d < 0.5) {
                /* Random Jump */
                int nq1 = 1 + rand()%10;
                int nq2 = 1 + rand()%10;
                int nq3 = 1 + rand()%10;
                double ns1 = (rand()%100)/10.0;
                double ns2 = (rand()%100)/10.0;
                double ns3 = (rand()%100)/10.0;
                
                players[i].gx = (nq1-1)*10.0 + ns1;
                players[i].gy = (nq2-1)*10.0 + ns2;
                players[i].gz = (nq3-1)*10.0 + ns3;
                
                players[i].state.q1 = nq1; players[i].state.q2 = nq2; players[i].state.q3 = nq3;
                players[i].state.s1 = ns1; players[i].state.s2 = ns2; players[i].state.s3 = ns3;
                
                players[i].nav_state = NAV_STATE_IDLE;
                players[i].warp_speed = 0;
                
                send_server_msg(i, "CRITICAL", "SPATIAL RIFT ENCOUNTERED! UNCONTROLLED SUBSPACE FOLDING IN PROGRESS!");
                send_server_msg(i, "HELMSMAN", "Teleportation complete. Sensors recalibrating to new position.");
                break;
            }
        }

        if (players[i].gx <= 0.001 && players[i].gy <= 0.001) {
            players[i].gx = (players[i].state.q1-1)*10.0 + players[i].state.s1;
            players[i].gy = (players[i].state.q2-1)*10.0 + players[i].state.s2;
            players[i].gz = (players[i].state.q3-1)*10.0 + players[i].state.s3;
        }

        if (players[i].nav_state == NAV_STATE_ALIGN || players[i].nav_state == NAV_STATE_ALIGN_IMPULSE) {
            players[i].nav_timer--;
            
            double diff_h = players[i].target_h - players[i].start_h;
            while (diff_h > 180.0) diff_h -= 360.0;
            while (diff_h < -180.0) diff_h += 360.0;
            
            double diff_m = players[i].target_m - players[i].start_m;
            
            double t = 1.0 - (double)players[i].nav_timer / 60.0;
            players[i].state.ent_h = players[i].start_h + diff_h * t;
            players[i].state.ent_m = players[i].start_m + diff_m * t;
            
            /* Normalize heading */
            while (players[i].state.ent_h >= 360.0) players[i].state.ent_h -= 360.0;
            while (players[i].state.ent_h < 0.0) players[i].state.ent_h += 360.0;

            if (players[i].nav_timer <= 0) {
                if (players[i].nav_state == NAV_STATE_ALIGN) {
                    players[i].nav_state = NAV_STATE_WARP;
                    double dist = sqrt(pow(players[i].target_gx - players[i].gx, 2) + pow(players[i].target_gy - players[i].gy, 2) + pow(players[i].target_gz - players[i].gz, 2));
                    players[i].nav_timer = (int)(dist / 10.0 * 90.0); if (players[i].nav_timer < 30) players[i].nav_timer = 30;
                    players[i].warp_speed = dist / players[i].nav_timer;
                } else {
                    players[i].nav_state = NAV_STATE_IMPULSE;
                    char msg[64]; sprintf(msg, "Impulse engaged at %.0f%%.", players[i].warp_speed * 200.0);
                    send_server_msg(i, "HELMSMAN", msg);
                }
            }
        }
        else if (players[i].nav_state == NAV_STATE_WARP) {
            players[i].nav_timer--;
            players[i].gx += players[i].dx * players[i].warp_speed;
            players[i].gy += players[i].dy * players[i].warp_speed;
            players[i].gz += players[i].dz * players[i].warp_speed;
            
            /* Recalculate local sector and quadrant for visualization */
            players[i].state.q1 = get_q_from_g(players[i].gx);
            players[i].state.q2 = get_q_from_g(players[i].gy);
            players[i].state.q3 = get_q_from_g(players[i].gz);
            players[i].state.s1 = players[i].gx - (players[i].state.q1 - 1) * 10.0;
            players[i].state.s2 = players[i].gy - (players[i].state.q2 - 1) * 10.0;
            players[i].state.s3 = players[i].gz - (players[i].state.q3 - 1) * 10.0;

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
                /* Booster impulse speed 10x for better reactivity */
                players[i].gx += players[i].dx * players[i].warp_speed * 10.0;
                players[i].gy += players[i].dy * players[i].warp_speed * 10.0;
                players[i].gz += players[i].dz * players[i].warp_speed * 10.0;
                
                players[i].state.q1 = get_q_from_g(players[i].gx);
                players[i].state.q2 = get_q_from_g(players[i].gy);
                players[i].state.q3 = get_q_from_g(players[i].gz);
                players[i].state.s1 = players[i].gx - (players[i].state.q1 - 1) * 10.0;
                players[i].state.s2 = players[i].gy - (players[i].state.q2 - 1) * 10.0;
                players[i].state.s3 = players[i].gz - (players[i].state.q3 - 1) * 10.0;
            } else {
                players[i].nav_state = NAV_STATE_IDLE;
                send_server_msg(i, "COMPUTER", "Impulse drive failure: Zero energy.");
            }
        }
        else if (players[i].nav_state == NAV_STATE_WORMHOLE) {
            players[i].nav_timer--;
            
            /* Sci-Fi Message Sequence */
            if (players[i].nav_timer == 180) 
                send_server_msg(i, "ENGINEERING", "Injecting exotic matter into local Schwarzschild metric...");
            else if (players[i].nav_timer == 130)
                send_server_msg(i, "SCIENCE", "Einstein-Rosen Bridge detected. Stabilizing singularity...");
            else if (players[i].nav_timer == 80)
                send_server_msg(i, "HELMSMAN", "Wormhole mouth stable. Entering event horizon.");

            /* Update Wormhole visual position in packet */
            players[i].state.wormhole = (NetPoint){(float)players[i].wx, (float)players[i].wy, (float)players[i].wz, 1};

            /* Move ship INTO the wormhole during the last phase */
            if (players[i].nav_timer < 60) {
                 /* Interpolate from current Pos to Wormhole Pos */
                 double cur_s1 = players[i].gx - (players[i].state.q1 - 1) * 10.0;
                 double cur_s2 = players[i].gy - (players[i].state.q2 - 1) * 10.0;
                 double cur_s3 = players[i].gz - (players[i].state.q3 - 1) * 10.0;
                 
                 /* Simple approach effect */
                 players[i].gx += (players[i].wx - cur_s1) * 0.05;
                 players[i].gy += (players[i].wy - cur_s2) * 0.05;
                 players[i].gz += (players[i].wz - cur_s3) * 0.05;
            }

            if (players[i].nav_timer <= 0) {
                /* JUMP! */
                players[i].gx = players[i].target_gx;
                players[i].gy = players[i].target_gy;
                players[i].gz = players[i].target_gz;
                
                players[i].nav_state = NAV_STATE_IDLE;
                players[i].state.wormhole.active = 0; /* Turn off visual */
                
                /* Trigger Sparkle Arrival Effect (Calculate local sector coords for destination) */
                int tq1 = get_q_from_g(players[i].gx);
                int tq2 = get_q_from_g(players[i].gy);
                int tq3 = get_q_from_g(players[i].gz);
                float ts1 = (float)(players[i].gx - (tq1 - 1) * 10.0);
                float ts2 = (float)(players[i].gy - (tq2 - 1) * 10.0);
                float ts3 = (float)(players[i].gz - (tq3 - 1) * 10.0);
                players[i].state.jump_arrival = (NetPoint){ts1, ts2, ts3, 1};
                
                send_server_msg(i, "HELMSMAN", "Wormhole traversal successful. Welcome to destination.");
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
            } else if (tid >= 10000 && tid < 10000+MAX_COMETS && comets[tid-10000].active) {
                int c = tid - 10000;
                tx = (comets[c].q1-1)*10.0 + comets[c].x;
                ty = (comets[c].q2-1)*10.0 + comets[c].y;
                tz = (comets[c].q3-1)*10.0 + comets[c].z;
                /* Approximate velocity from heading/mark */
                double rad_h = comets[c].h * M_PI / 180.0;
                double rad_m = comets[c].m * M_PI / 180.0;
                tvx = cos(rad_m) * sin(rad_h) * 0.02; 
                tvy = cos(rad_m) * -cos(rad_h) * 0.02;
                tvz = sin(rad_m) * 0.02;
                tq1 = comets[c].q1; tq2 = comets[c].q2; tq3 = comets[c].q3;
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
        for (int h = 0; h < current_q->bh_count; h++) {
            double d = sqrt(pow(players[i].state.s1 - current_q->black_holes[h]->x, 2) + pow(players[i].state.s2 - current_q->black_holes[h]->y, 2) + pow(players[i].state.s3 - current_q->black_holes[h]->z, 2));
            if (d < 2.5) {
                /* Gravity Well Effect: Drain Shields and Energy */
                int drain = (int)((2.5 - d) * 1000.0);
                for(int s=0; s<6; s++) { if(players[i].state.shields[s] > 0) players[i].state.shields[s] -= (drain/10); if(players[i].state.shields[s] < 0) players[i].state.shields[s] = 0; }
                players[i].state.energy -= drain;
                if (global_tick % 20 == 0) send_server_msg(i, "WARNING", "Extreme gravitational shear detected! Hull integrity at risk.");
            }
            if (d < 0.6) { 
                send_server_msg(i, "CRITICAL", "Event Horizon crossed! Spaghettification in progress..."); 
                players[i].state.energy = 0; players[i].state.crew_count = 0;
                players[i].nav_state = NAV_STATE_IDLE; players[i].warp_speed = 0;
                players[i].dx = 0; players[i].dy = 0; players[i].dz = 0;
                players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1}; 
                break; 
            }
        }
        if (players[i].active && players[i].state.energy > 0) for (int s = 0; s < current_q->star_count; s++) {
            double d = sqrt(pow(players[i].state.s1 - current_q->stars[s]->x, 2) + pow(players[i].state.s2 - current_q->stars[s]->y, 2) + pow(players[i].state.s3 - current_q->stars[s]->z, 2));
            if (d < 0.8) { 
                send_server_msg(i, "CRITICAL", "Impact with star corona! Hull melting..."); 
                players[i].state.energy = 0; players[i].state.crew_count = 0;
                players[i].nav_state = NAV_STATE_IDLE; players[i].warp_speed = 0;
                players[i].dx = 0; players[i].dy = 0; players[i].dz = 0;
                players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1}; 
                break; 
            }
        }
        if (players[i].active && players[i].state.energy > 0) for (int p = 0; p < current_q->planet_count; p++) {
            double d = sqrt(pow(players[i].state.s1 - current_q->planets[p]->x, 2) + pow(players[i].state.s2 - current_q->planets[p]->y, 2) + pow(players[i].state.s3 - current_q->planets[p]->z, 2));
            if (d < 0.8) { 
                send_server_msg(i, "CRITICAL", "Planetary collision! Structural failure."); 
                players[i].state.energy = 0; players[i].state.crew_count = 0;
                players[i].nav_state = NAV_STATE_IDLE; players[i].warp_speed = 0;
                players[i].dx = 0; players[i].dy = 0; players[i].dz = 0;
                players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1}; 
                break; 
            }
        }

        /* Target Lock Validation (Inter-Quadrant aware) */
        if (players[i].state.lock_target > 0) {
            int tid = players[i].state.lock_target;
            bool valid = false;
            int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
            
            if (tid >= 1 && tid <= 32) {
                /* Players can be locked as long as they are active anywhere */
                if (players[tid-1].active) valid = true;
            } else if (tid >= 1000 && tid < 1000+MAX_NPC) {
                if (npcs[tid-1000].active) valid = true;
            } else if (tid >= 2000 && tid < 2000+MAX_BASES) {
                /* Static objects remain local-only for locking sanity */
                if (bases[tid-2000].active && bases[tid-2000].q1 == pq1 && bases[tid-2000].q2 == pq2 && bases[tid-2000].q3 == pq3) valid = true;
            } else if (tid >= 3000 && tid < 3000+MAX_PLANETS) {
                if (planets[tid-3000].active && planets[tid-3000].q1 == pq1 && planets[tid-3000].q2 == pq2 && planets[tid-3000].q3 == pq3) valid = true;
            } else if (tid >= 4000 && tid < 4000+MAX_STARS) {
                if (stars_data[tid-4000].active && stars_data[tid-4000].q1 == pq1 && stars_data[tid-4000].q2 == pq2 && stars_data[tid-4000].q3 == pq3) valid = true;
            } else if (tid >= 7000 && tid < 7000+MAX_BH) {
                if (black_holes[tid-7000].active && black_holes[tid-7000].q1 == pq1 && black_holes[tid-7000].q2 == pq2 && black_holes[tid-7000].q3 == pq3) valid = true;
            } else if (tid >= 8000 && tid < 8000+MAX_NEBULAS) {
                if (nebulas[tid-8000].active && nebulas[tid-8000].q1 == pq1 && nebulas[tid-8000].q2 == pq2 && nebulas[tid-8000].q3 == pq3) valid = true;
            } else if (tid >= 9000 && tid < 9000+MAX_PULSARS) {
                if (pulsars[tid-9000].active && pulsars[tid-9000].q1 == pq1 && pulsars[tid-9000].q2 == pq2 && pulsars[tid-9000].q3 == pq3) valid = true;
            } else if (tid >= 10000 && tid < 10000+MAX_COMETS) {
                if (comets[tid-10000].active) valid = true; /* Persistent across quadrants */
            } else if (tid >= 11000 && tid < 11000+MAX_DERELICTS) {
                if (derelicts[tid-11000].active && derelicts[tid-11000].q1 == pq1 && derelicts[tid-11000].q2 == pq2 && derelicts[tid-11000].q3 == pq3) valid = true;
            } else if (tid >= 12000 && tid < 12000+MAX_ASTEROIDS) {
                if (asteroids[tid-12000].active && asteroids[tid-12000].q1 == pq1 && asteroids[tid-12000].q2 == pq2 && asteroids[tid-12000].q3 == pq3) valid = true;
            } else if (tid >= 14000 && tid < 14000+MAX_MINES) {
                if (mines[tid-14000].active && mines[tid-14000].q1 == pq1 && mines[tid-14000].q2 == pq2 && mines[tid-14000].q3 == pq3) valid = true;
            } else if (tid >= 15000 && tid < 15000+MAX_BUOYS) {
                if (buoys[tid-15000].active && buoys[tid-15000].q1 == pq1 && buoys[tid-15000].q2 == pq2 && buoys[tid-15000].q3 == pq3) valid = true;
            } else if (tid >= 16000 && tid < 16000+MAX_PLATFORMS) {
                if (platforms[tid-16000].active) valid = true; /* Persistent */
            } else if (tid >= 17000 && tid < 17000+MAX_RIFTS) {
                if (rifts[tid-17000].active && rifts[tid-17000].q1 == pq1 && rifts[tid-17000].q2 == pq2 && rifts[tid-17000].q3 == pq3) valid = true;
            } else if (tid >= 18000 && tid < 18000+MAX_MONSTERS) {
                if (monsters[tid-18000].active) valid = true; /* Persistent */
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
                } else if (tid >= 1000 && tid < 1000+MAX_NPC && npcs[tid-1000].active && npcs[tid-1000].q1 == pq1 && npcs[tid-1000].q2 == pq2 && npcs[tid-1000].q3 == pq3) {
                    target_x = npcs[tid-1000].x; target_y = npcs[tid-1000].y; target_z = npcs[tid-1000].z;
                } else if (tid >= 16000 && tid < 16000+MAX_PLATFORMS && platforms[tid-16000].active && platforms[tid-16000].q1 == pq1 && platforms[tid-16000].q2 == pq2 && platforms[tid-16000].q3 == pq3) {
                    target_x = platforms[tid-16000].x; target_y = platforms[tid-16000].y; target_z = platforms[tid-16000].z;
                } else if (tid >= 18000 && tid < 18000+MAX_MONSTERS && monsters[tid-18000].active && monsters[tid-18000].q1 == pq1 && monsters[tid-18000].q2 == pq2 && monsters[tid-18000].q3 == pq3) {
                    target_x = monsters[tid-18000].x; target_y = monsters[tid-18000].y; target_z = monsters[tid-18000].z;
                }
                if (target_x != -1) {
                    double dx = target_x - players[i].tx, dy = target_y - players[i].ty, dz = target_z - players[i].tz;
                    double d = sqrt(dx*dx + dy*dy + dz*dz);
                    if (d > 0.01) {
                        /* Aggressive guidance: 50% correction per tick */
                        players[i].tdx = (players[i].tdx * 0.5) + ((dx/d) * 0.5);
                        players[i].tdy = (players[i].tdy * 0.5) + ((dy/d) * 0.5);
                        players[i].tdz = (players[i].tdz * 0.5) + ((dz/d) * 0.5);
                        double s = sqrt(players[i].tdx*players[i].tdx + players[i].tdy*players[i].tdy + players[i].tdz*players[i].tdz);
                        players[i].tdx /= s; players[i].tdy /= s; players[i].tdz /= s;
                    }
                }
            }
            /* Increased velocity: 0.25 units per tick */
            players[i].tx += players[i].tdx * 0.25; players[i].ty += players[i].tdy * 0.25; players[i].tz += players[i].tdz * 0.25;
            players[i].state.torp = (NetPoint){(float)players[i].tx, (float)players[i].ty, (float)players[i].tz, 1};
            
            /* Collision Detection (Radius increased to 0.8 to prevent tunneling) */
            bool hit = false;
            QuadrantIndex *lq = &spatial_index[players[i].state.q1][players[i].state.q2][players[i].state.q3];
            
            /* 1. Players */
            for (int j=0; j<lq->player_count; j++) {
                ConnectedPlayer *p = lq->players[j]; if (p == &players[i] || !p->active) continue;
                double d = sqrt(pow(players[i].tx - p->state.s1, 2) + pow(players[i].ty - p->state.s2, 2) + pow(players[i].tz - p->state.s3, 2));
                if (d < 0.8) {
                    int dmg = 75000;
                    for(int s=0;s<6;s++) { int abs=(p->state.shields[s]>=dmg/6)?dmg/6:p->state.shields[s]; p->state.shields[s]-=abs; dmg-=abs; }
                    p->state.energy -= dmg; send_server_msg((int)(p-players), "WARNING", "HIT BY PHOTON TORPEDO!");
                    if(p->state.energy <= 0) { 
                        p->state.energy = 0; p->state.crew_count = 0;
                        p->nav_state = NAV_STATE_IDLE; p->warp_speed = 0;
                        p->state.boom = (NetPoint){(float)p->state.s1, (float)p->state.s2, (float)p->state.s3, 1}; 
                    }
                    hit = true; break;
                }
            }
            /* 2. NPCs */
            if (!hit) for (int n=0; n<lq->npc_count; n++) {
                NPCShip *npc = lq->npcs[n];
                double d = sqrt(pow(players[i].tx - npc->x, 2) + pow(players[i].ty - npc->y, 2) + pow(players[i].tz - npc->z, 2));
                if (d < 0.8) { npc->energy -= 75000; if(npc->energy <= 0) { npc->active = 0; players[i].state.boom = (NetPoint){(float)npc->x, (float)npc->y, (float)npc->z, 1}; } hit = true; break; }
            }
            /* 3. Planets/Stars/Bases (Solid obstacles) */
            if (!hit) for (int p=0; p<lq->planet_count; p++) {
                double d = sqrt(pow(players[i].tx - lq->planets[p]->x, 2) + pow(players[i].ty - lq->planets[p]->y, 2) + pow(players[i].tz - lq->planets[p]->z, 2));
                if (d < 1.2) { hit = true; break; } /* Planet hit (absorbed) */
            }
            if (!hit) for (int s=0; s<lq->star_count; s++) {
                double d = sqrt(pow(players[i].tx - lq->stars[s]->x, 2) + pow(players[i].ty - lq->stars[s]->y, 2) + pow(players[i].tz - lq->stars[s]->z, 2));
                if (d < 1.5) { hit = true; break; } /* Star hit (vaporized) */
            }
            if (!hit) for (int b=0; b<lq->base_count; b++) {
                double d = sqrt(pow(players[i].tx - lq->bases[b]->x, 2) + pow(players[i].ty - lq->bases[b]->y, 2) + pow(players[i].tz - lq->bases[b]->z, 2));
                if (d < 1.0) { hit = true; break; } /* Base hit (absorbed by planetary shields) */
            }
            /* 4. Platforms/Monsters */
            if (!hit) for (int pt=0; pt<lq->platform_count; pt++) {
                NPCPlatform *plat = lq->platforms[pt];
                double d = sqrt(pow(players[i].tx - plat->x, 2) + pow(players[i].ty - plat->y, 2) + pow(players[i].tz - plat->z, 2));
                if (d < 0.8) { plat->energy -= 50000; if(plat->energy <= 0) { plat->active = 0; players[i].state.boom = (NetPoint){(float)plat->x, (float)plat->y, (float)plat->z, 1}; } hit = true; break; }
            }
            if (!hit) for (int mo=0; mo<lq->monster_count; mo++) {
                NPCMonster *mon = lq->monsters[mo];
                double d = sqrt(pow(players[i].tx - mon->x, 2) + pow(players[i].ty - mon->y, 2) + pow(players[i].tz - mon->z, 2));
                if (d < 1.0) { mon->energy -= 100000; if(mon->energy <= 0) { mon->active = 0; players[i].state.boom = (NetPoint){(float)mon->x, (float)mon->y, (float)mon->z, 1}; } hit = true; break; }
            }
            if (hit || players[i].tx<0||players[i].tx>10||players[i].ty<0||players[i].ty>10||players[i].tz<0||players[i].tz>10) {
                if (hit) { players[i].state.boom = (NetPoint){(float)players[i].tx, (float)players[i].ty, (float)players[i].tz, 1}; send_server_msg(i, "TACTICAL", "Torpedo impact confirmed."); }
                players[i].torp_active = false; players[i].state.torp.active = 0;
            }
        }
    }

    rebuild_spatial_index();
    if (global_tick % 1800 == 0) save_galaxy();

    /* Phase 3: Network Updates - Atomic Broadcast */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].socket == 0) continue;
        PacketUpdate upd; memset(&upd, 0, sizeof(PacketUpdate)); upd.type = PKT_UPDATE;
        upd.q1 = players[i].state.q1; upd.q2 = players[i].state.q2; upd.q3 = players[i].state.q3;
        upd.s1 = players[i].state.s1; upd.s2 = players[i].state.s2; upd.s3 = players[i].state.s3;
        upd.ent_h = players[i].state.ent_h; upd.ent_m = players[i].state.ent_m;
        upd.energy = players[i].state.energy; upd.torpedoes = players[i].state.torpedoes;
        upd.cargo_energy = players[i].state.cargo_energy; upd.cargo_torpedoes = players[i].state.cargo_torpedoes;
        upd.crew_count = players[i].state.crew_count;
        for(int s=0; s<6; s++) upd.shields[s] = players[i].state.shields[s];
        for(int inv=0; inv<7; inv++) upd.inventory[inv] = players[i].state.inventory[inv];
        for(int sys=0; sys<8; sys++) upd.system_health[sys] = players[i].state.system_health[sys];
        upd.lock_target = players[i].state.lock_target;
        upd.is_cloaked = players[i].state.is_cloaked;
        upd.encryption_enabled = players[i].crypto_algo;
        int o_idx = 0;
        upd.objects[o_idx] = (NetObject){(float)players[i].state.s1,(float)players[i].state.s2,(float)players[i].state.s3,(float)players[i].state.ent_h,(float)players[i].state.ent_m,1,players[i].ship_class,1,(int)((players[i].state.energy/1000000.0)*100),i+1,""};
        strncpy(upd.objects[o_idx++].name, players[i].name, 63);
        
        /* 1. Prioritize Current Quadrant Objects (Critical for SRS/HUD) */
        if (IS_Q_VALID(upd.q1, upd.q2, upd.q3)) {
            QuadrantIndex *lq = &spatial_index[upd.q1][upd.q2][upd.q3];
            /* Players in current quadrant */
            for(int j=0; j<lq->player_count; j++) {
                ConnectedPlayer *p = lq->players[j]; 
                if (p == &players[i] || !p->active || o_idx >= MAX_NET_OBJECTS) continue;
                if (p->state.is_cloaked && p->faction != players[i].faction) continue;
                NetObject *no = &upd.objects[o_idx];
                *no = (NetObject){(float)p->state.s1, (float)p->state.s2, (float)p->state.s3, (float)p->state.ent_h, (float)p->state.ent_m, 1, p->ship_class, 1, (int)((p->state.energy/1000000.0)*100), (int)(p-players)+1, ""};
                strncpy(no->name, p->name, 63); o_idx++;
            }
            /* NPCs in current quadrant */
            for(int n=0; n<lq->npc_count && o_idx < MAX_NET_OBJECTS; n++) {
                NPCShip *npc = lq->npcs[n]; if (!npc->active) continue;
                int max_e = (npc->faction==FACTION_BORG)?100000:50000;
                NetObject *no = &upd.objects[o_idx];
                *no = (NetObject){(float)npc->x, (float)npc->y, (float)npc->z, (float)npc->h, (float)npc->m, npc->faction, 0, 1, (int)((npc->energy/(float)max_e)*100), npc->id+1000, ""};
                strncpy(no->name, get_species_name(npc->faction), 63); o_idx++;
            }
            /* Static objects in current quadrant */
            for(int p=0; p<lq->planet_count && o_idx < MAX_NET_OBJECTS; p++) if(lq->planets[p]->active) upd.objects[o_idx++] = (NetObject){(float)lq->planets[p]->x, (float)lq->planets[p]->y, (float)lq->planets[p]->z, 0, 0, 5, lq->planets[p]->resource_type, 1, 100, lq->planets[p]->id+3000, "Planet"};
            for(int s=0; s<lq->star_count && o_idx < MAX_NET_OBJECTS; s++) if(lq->stars[s]->active) upd.objects[o_idx++] = (NetObject){(float)lq->stars[s]->x, (float)lq->stars[s]->y, (float)lq->stars[s]->z, 0, 0, 4, lq->stars[s]->id % 7, 1, 100, lq->stars[s]->id+4000, "Star"};
            for(int h=0; h<lq->bh_count && o_idx < MAX_NET_OBJECTS; h++) if(lq->black_holes[h]->active) upd.objects[o_idx++] = (NetObject){(float)lq->black_holes[h]->x, (float)lq->black_holes[h]->y, (float)lq->black_holes[h]->z, 0, 0, 6, 0, 1, 100, lq->black_holes[h]->id+7000, "Black Hole"};
            for(int b=0; b<lq->base_count && o_idx < MAX_NET_OBJECTS; b++) if(lq->bases[b]->active) upd.objects[o_idx++] = (NetObject){(float)lq->bases[b]->x, (float)lq->bases[b]->y, (float)lq->bases[b]->z, 0, 0, 3, 0, 1, 100, lq->bases[b]->id+2000, "Starbase"};
            for(int n=0; n<lq->nebula_count && o_idx < MAX_NET_OBJECTS; n++) upd.objects[o_idx++] = (NetObject){(float)lq->nebulas[n]->x, (float)lq->nebulas[n]->y, (float)lq->nebulas[n]->z, 0, 0, 7, lq->nebulas[n]->id % 5, 1, 100, lq->nebulas[n]->id+8000, "Nebula"};
            for(int p=0; p<lq->pulsar_count && o_idx < MAX_NET_OBJECTS; p++) upd.objects[o_idx++] = (NetObject){(float)lq->pulsars[p]->x, (float)lq->pulsars[p]->y, (float)lq->pulsars[p]->z, 0, 0, 8, 0, 1, 100, lq->pulsars[p]->id+9000, "Pulsar"};
            for(int c=0; c<lq->comet_count && o_idx < MAX_NET_OBJECTS; c++) upd.objects[o_idx++] = (NetObject){(float)lq->comets[c]->x, (float)lq->comets[c]->y, (float)lq->comets[c]->z, (float)lq->comets[c]->h, (float)lq->comets[c]->m, 9, 0, 1, 100, lq->comets[c]->id+10000, "Comet"};
            for(int a=0; a<lq->asteroid_count && o_idx < MAX_NET_OBJECTS; a++) upd.objects[o_idx++] = (NetObject){(float)lq->asteroids[a]->x, (float)lq->asteroids[a]->y, (float)lq->asteroids[a]->z, 0, 0, 21, 0, 1, 100, lq->asteroids[a]->id+12000, "Asteroid"};
            for(int d=0; d<lq->derelict_count && o_idx < MAX_NET_OBJECTS; d++) upd.objects[o_idx++] = (NetObject){(float)lq->derelicts[d]->x, (float)lq->derelicts[d]->y, (float)lq->derelicts[d]->z, 0, 0, 22, lq->derelicts[d]->ship_class, 1, 30, lq->derelicts[d]->id+11000, "Derelict"};
            for(int pt=0; pt<lq->platform_count && o_idx < MAX_NET_OBJECTS; pt++) upd.objects[o_idx++] = (NetObject){(float)lq->platforms[pt]->x, (float)lq->platforms[pt]->y, (float)lq->platforms[pt]->z, 0, 0, 25, 0, 1, (int)((lq->platforms[pt]->energy/10000.0)*100), lq->platforms[pt]->id+16000, "Defense Platform"};
            for(int mo=0; mo<lq->monster_count && o_idx < MAX_NET_OBJECTS; mo++) { NetObject *no = &upd.objects[o_idx++]; *no = (NetObject){(float)lq->monsters[mo]->x, (float)lq->monsters[mo]->y, (float)lq->monsters[mo]->z, 0, 0, lq->monsters[mo]->type, 0, 1, 100, lq->monsters[mo]->id+18000, ""}; strncpy(no->name, (lq->monsters[mo]->type==30)?"Crystalline Entity":"Space Amoeba", 63); }
        }

        upd.object_count = o_idx;
        upd.beam_count = players[i].state.beam_count; for(int b=0; b<upd.beam_count && b<MAX_NET_BEAMS; b++) upd.beams[b] = players[i].state.beams[b];
        
        /* Map Synchronizer: Always send supernova quadrant if active, otherwise send current */
        if (supernova_event.supernova_timer > 0) {
            upd.map_update_q[0] = supernova_event.supernova_q1;
            upd.map_update_q[1] = supernova_event.supernova_q2;
            upd.map_update_q[2] = supernova_event.supernova_q3;
            upd.map_update_val = -supernova_event.supernova_timer;
        } else {
            upd.map_update_q[0] = upd.q1;
            upd.map_update_q[1] = upd.q2;
            upd.map_update_q[2] = upd.q3;
            upd.map_update_val = galaxy_master.g[upd.q1][upd.q2][upd.q3];
        }

        upd.torp = players[i].state.torp; upd.boom = players[i].state.boom; upd.dismantle = players[i].state.dismantle; 
        upd.wormhole = players[i].state.wormhole;
        upd.jump_arrival = players[i].state.jump_arrival;
        
        if (supernova_event.supernova_timer > 0) {
            upd.supernova_pos = (NetPoint){(float)supernova_event.x, (float)supernova_event.y, (float)supernova_event.z, supernova_event.supernova_timer};
            upd.supernova_q[0] = supernova_event.supernova_q1;
            upd.supernova_q[1] = supernova_event.supernova_q2;
            upd.supernova_q[2] = supernova_event.supernova_q3;
        } else {
            upd.supernova_pos.active = 0;
        }

        players[i].state.beam_count = 0; players[i].state.boom.active = 0; players[i].state.dismantle.active = 0; players[i].state.wormhole.active = 0;
        players[i].state.jump_arrival.active = 0;
        int current_sock = players[i].socket;
                if (current_sock != 0) { size_t p_size = sizeof(PacketUpdate) - sizeof(NetObject) * (MAX_NET_OBJECTS - upd.object_count); if (p_size < offsetof(PacketUpdate, objects)) p_size = offsetof(PacketUpdate, objects); write_all(current_sock, &upd, p_size); }
            }
            pthread_mutex_unlock(&game_mutex);
        }
        