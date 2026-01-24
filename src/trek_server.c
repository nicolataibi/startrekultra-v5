/* 
 * STARTREK ULTRA - 3D LOGIC ENGINE 
 * Authors: Nicola Taibi, Supported by Google Gemini
 * Copyright (C) 2026 Nicola Taibi
 * License: GNU General Public License v3.0
 */

#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <time.h>
#include <stddef.h>
#include <signal.h>
#include <errno.h>
#include <math.h>
#include "server_internal.h"

#define MAX_EVENTS (MAX_CLIENTS + 10)

pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;
int g_debug = 0;

void *game_loop_thread(void *arg) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    while (1) {
        ts.tv_nsec += 33333333;
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
        update_game_logic();
    }
}

int main(int argc, char *argv[]) {
    int server_fd, epoll_fd;
    struct sockaddr_in addr;
    int opt = 1, adlen = sizeof(addr);
    struct epoll_event ev, events[MAX_EVENTS];

    for (int i = 1; i < argc; i++) if (strcmp(argv[i], "-d") == 0) g_debug = 1;
    signal(SIGPIPE, SIG_IGN);
    
    memset(players, 0, sizeof(players)); srand(time(NULL)); 
    
    if (!load_galaxy()) { generate_galaxy(); save_galaxy(); }
    init_static_spatial_index();
    
    pthread_t tid; pthread_create(&tid, NULL, game_loop_thread, NULL);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(DEFAULT_PORT);
    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)); listen(server_fd, 10);

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) { perror("epoll_create1"); exit(EXIT_FAILURE); }

    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) { perror("epoll_ctl: server_fd"); exit(EXIT_FAILURE); }

    printf("TREK SERVER started on port %d (EPOLL MODE)\n", DEFAULT_PORT);
    
    while (1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait"); break;
        }

        for (int n = 0; n < nfds; ++n) {
            int fd = events[n].data.fd;

            if (fd == server_fd) {
                int new_socket = accept(server_fd, (struct sockaddr *)&addr, (socklen_t*)&adlen);
                if (new_socket == -1) { perror("accept"); continue; }
                
                ev.events = EPOLLIN | EPOLLET; /* Edge Triggered for performance */
                ev.data.fd = new_socket;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_socket, &ev) == -1) { perror("epoll_ctl: new_socket"); close(new_socket); }
                LOG_DEBUG("New connection accepted: FD %d\n", new_socket);
            } else {
                /* Handle data from a client */
                int type;
                int r = read_all(fd, &type, sizeof(int));
                
                if (r <= 0) {
                    /* Disconnect */
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    pthread_mutex_lock(&game_mutex);
                    for (int i=0; i<MAX_CLIENTS; i++) if (players[i].socket == fd) { players[i].socket = 0; players[i].active = 0; break; }
                    pthread_mutex_unlock(&game_mutex);
                    close(fd);
                    LOG_DEBUG("Connection closed: FD %d\n", fd);
                    continue;
                }

                /* Find player index if already logged in */
                int p_idx = -1;
                pthread_mutex_lock(&game_mutex);
                for (int i=0; i<MAX_CLIENTS; i++) if (players[i].socket == fd && players[i].active) { p_idx = i; break; }
                pthread_mutex_unlock(&game_mutex);

                if (type == PKT_QUERY || type == PKT_LOGIN) {
                    PacketLogin pkt;
                    if (read_all(fd, ((char*)&pkt) + sizeof(int), sizeof(PacketLogin) - sizeof(int)) > 0) {
                        if (type == PKT_QUERY) {
                            pthread_mutex_lock(&game_mutex);
                            int found = 0;
                            for(int j=0; j<MAX_CLIENTS; j++) { if (players[j].name[0] != '\0' && strcmp(players[j].name, pkt.name) == 0) { found = 1; break; } }
                            pthread_mutex_unlock(&game_mutex);
                            write_all(fd, &found, sizeof(int));
                        } else {
                            pthread_mutex_lock(&game_mutex);
                            int slot = -1;
                            for(int j=0; j<MAX_CLIENTS; j++) { if (players[j].name[0] != '\0' && strcmp(players[j].name, pkt.name) == 0) { slot = j; break; } }
                            if (slot == -1) { for(int j=0; j<MAX_CLIENTS; j++) if (players[j].name[0] == '\0') { slot = j; break; } }
                            
                            if (slot != -1) {
                                if (players[slot].socket != 0 && players[slot].socket != fd) {
                                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, players[slot].socket, NULL);
                                    close(players[slot].socket);
                                }
                                players[slot].socket = fd;
                                int is_new = (players[slot].name[0] == '\0');
                                if (is_new) {
                                    strcpy(players[slot].name, pkt.name); players[slot].faction = pkt.faction; players[slot].ship_class = pkt.ship_class;
                                    players[slot].state.energy = 100000; players[slot].state.torpedoes = 100;
                                    int crew = 400;
                                    switch(pkt.ship_class) {
                                        case SHIP_CLASS_GALAXY:    crew = 1012; break;
                                        case SHIP_CLASS_SOVEREIGN: crew = 850; break;
                                        case SHIP_CLASS_CONSTITUTION: crew = 430; break;
                                        case SHIP_CLASS_EXCELSIOR: crew = 750; break;
                                        case SHIP_CLASS_DEFIANT:   crew = 50; break;
                                        case SHIP_CLASS_INTREPID:  crew = 150; break;
                                        case SHIP_CLASS_OBERTH:    crew = 80; break;
                                        default: crew = 200; break;
                                    }
                                    players[slot].state.crew_count = crew;
                                    players[slot].state.q1 = rand()%10 + 1; players[slot].state.q2 = rand()%10 + 1; players[slot].state.q3 = rand()%10 + 1;
                                    players[slot].state.s1 = 5.0; players[slot].state.s2 = 5.0; players[slot].state.s3 = 5.0;
                                    for(int s=0; s<8; s++) players[slot].state.system_health[s] = 100.0f;
                                }
                                pthread_mutex_unlock(&game_mutex);

                                LOG_DEBUG("Synchronizing Galaxy Master (%zu bytes) to FD %d\n", sizeof(StarTrekGame), fd);
                                if (write_all(fd, &galaxy_master, sizeof(StarTrekGame)) == sizeof(StarTrekGame)) {
                                    pthread_mutex_lock(&game_mutex);
                                    LOG_DEBUG("Galaxy Master sent successfully to FD %d\n", fd);
                                    bool needs_rescue = false;
                                    if (players[slot].state.energy <= 0 || players[slot].state.crew_count <= 0) needs_rescue = true;
                                    
                                    int pq1 = players[slot].state.q1, pq2 = players[slot].state.q2, pq3 = players[slot].state.q3;
                                    if (IS_Q_VALID(pq1, pq2, pq3)) {
                                        QuadrantIndex *qi = &spatial_index[pq1][pq2][pq3];
                                        for (int s=0; s<qi->star_count; s++) {
                                            double d = sqrt(pow(players[slot].state.s1 - qi->stars[s]->x, 2) + pow(players[slot].state.s2 - qi->stars[s]->y, 2) + pow(players[slot].state.s3 - qi->stars[s]->z, 2));
                                            if (d < 1.0) needs_rescue = true;
                                        }
                                        for (int p=0; p<qi->planet_count; p++) {
                                            double d = sqrt(pow(players[slot].state.s1 - qi->planets[p]->x, 2) + pow(players[slot].state.s2 - qi->planets[p]->y, 2) + pow(players[slot].state.s3 - qi->planets[p]->z, 2));
                                            if (d < 1.0) needs_rescue = true;
                                        }
                                    }

                                    if (needs_rescue) {
                                        players[slot].state.q1 = rand()%10 + 1; players[slot].state.q2 = rand()%10 + 1; players[slot].state.q3 = rand()%10 + 1;
                                        players[slot].state.s1 = 5.0; players[slot].state.s2 = 5.0; players[slot].state.s3 = 5.0;
                                        players[slot].state.energy = 50000;
                                        if (players[slot].state.crew_count <= 0) players[slot].state.crew_count = 100;
                                        for(int s=0; s<8; s++) players[slot].state.system_health[s] = 80.0f;
                                        players[slot].gx = (players[slot].state.q1-1)*10.0 + 5.0;
                                        players[slot].gy = (players[slot].state.q2-1)*10.0 + 5.0;
                                        players[slot].gz = (players[slot].state.q3-1)*10.0 + 5.0;
                                        players[slot].nav_state = NAV_STATE_IDLE; players[slot].warp_speed = 0;
                                        players[slot].dx = 0; players[slot].dy = 0; players[slot].dz = 0;
                                        players[slot].active = 1;
                                        pthread_mutex_unlock(&game_mutex);
                                        send_server_msg(slot, "STARFLEET", "EMERGENCY RESCUE: Your ship was recovered from a collision zone and towed to a safe sector.");
                                    } else {
                                        players[slot].active = 1;
                                        pthread_mutex_unlock(&game_mutex);
                                        send_server_msg(slot, "SERVER", is_new ? "Welcome aboard, new Captain." : "Commander, welcome back.");
                                    }
                                }
                            } else {
                                pthread_mutex_unlock(&game_mutex);
                            }
                        }
                    }
                } else if (p_idx != -1) {
                    if (type == PKT_COMMAND) {
                        PacketCommand pkt;
                        if (read_all(fd, ((char*)&pkt) + sizeof(int), sizeof(PacketCommand) - sizeof(int)) > 0) process_command(p_idx, pkt.cmd);
                    } else if (type == PKT_MESSAGE) {
                        PacketMessage pkt;
                        if (read_all(fd, ((char*)&pkt) + sizeof(int), offsetof(PacketMessage, text) - sizeof(int)) > 0) {
                            if (pkt.length > 0 && pkt.length < 4096) read_all(fd, pkt.text, pkt.length + 1);
                            else pkt.text[0] = '\0';
                            pkt.type = type; broadcast_message(&pkt);
                        }
                    }
                }
            }
        }
    }
    return 0;
}