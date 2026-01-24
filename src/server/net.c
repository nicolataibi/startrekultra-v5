/* 
 * STARTREK ULTRA - 3D LOGIC ENGINE 
 * Authors: Nicola Taibi, Supported by Google Gemini
 * Copyright (C) 2026 Nicola Taibi
 * License: GNU General Public License v3.0
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <sys/socket.h>
#include <unistd.h>
#include "server_internal.h"
#include "ui.h"

int read_all(int fd, void *buf, size_t len) {
    size_t total = 0;
    char *p = (char *)buf;
    while (total < len) {
        ssize_t n = read(fd, p + total, len - total);
        if (n == 0) return 0;
        if (n < 0) return -1;
        total += n;
    }
    return (int)total;
}

int write_all(int fd, const void *buf, size_t len) {
    size_t total = 0;
    const char *p = (char *)buf;
    while (total < len) {
        ssize_t n = send(fd, p + total, len - total, 0);
        if (n <= 0) return (int)n;
        total += n;
    }
    return (int)total;
}

void broadcast_message(PacketMessage *msg) {
    int sockets[MAX_CLIENTS];
    int factions[MAX_CLIENTS];
    int count = 0;

    pthread_mutex_lock(&game_mutex);
    msg->length = strlen(msg->text);
    size_t pkt_size = offsetof(PacketMessage, text) + msg->length + 1;
    if (pkt_size > sizeof(PacketMessage)) pkt_size = sizeof(PacketMessage);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].active && players[i].socket != 0) {
            if (msg->scope == SCOPE_FACTION && players[i].faction != msg->faction) continue;
            if (msg->scope == SCOPE_PRIVATE) {
                bool is_target = ((i + 1) == msg->target_id);
                bool is_sender = (strcmp(players[i].name, msg->from) == 0);
                if (!is_target && !is_sender) continue;
            }
            sockets[count] = players[i].socket;
            factions[count] = players[i].faction;
            count++;
        }
    }
    pthread_mutex_unlock(&game_mutex);

    for (int i = 0; i < count; i++) {
        write_all(sockets[i], msg, pkt_size);
    }
}

void send_server_msg(int p_idx, const char *from, const char *text) {
    PacketMessage msg;
    memset(&msg, 0, sizeof(PacketMessage));
    msg.type = PKT_MESSAGE;
    strncpy(msg.from, from, 63);
    strncpy(msg.text, text, 4095);
    msg.length = strlen(msg.text);
    size_t pkt_size = offsetof(PacketMessage, text) + msg.length + 1;
    if (pkt_size > sizeof(PacketMessage)) pkt_size = sizeof(PacketMessage);
    write_all(players[p_idx].socket, &msg, pkt_size);
}