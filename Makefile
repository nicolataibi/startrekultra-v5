# STARTREK ULTRA - 3D LOGIC ENGINE 
# Authors: Nicola Taibi, Supported by Google Gemini
# Copyright (C) 2026 Nicola Taibi
# License: GNU General Public License v3.0

CC = gcc
OPT_CFLAGS := -g -O2
CFLAGS += -Wall -Iinclude -std=c2x -D_XOPEN_SOURCE=700 $(OPT_CFLAGS)
GL_LIBS = -lglut -lGLU -lGL -lGLEW
SHM_LIBS = -lrt -lpthread -lcrypto -lm

all: trek_server trek_client trek_3dview trek_galaxy_viewer

SERVER_SRCS = src/trek_server.c src/server/galaxy.c src/server/net.c src/server/commands.c src/server/logic.c

trek_server: $(SERVER_SRCS)
	$(CC) $(SERVER_SRCS) -o trek_server $(CFLAGS) $(SHM_LIBS)

trek_galaxy_viewer: src/galaxy_viewer.c
	$(CC) src/galaxy_viewer.c -o trek_galaxy_viewer $(CFLAGS) $(SHM_LIBS)

trek_client: src/trek_client.c
	$(CC) src/trek_client.c -o trek_client $(CFLAGS) $(SHM_LIBS)

trek_3dview: src/trek_3dview.c
	$(CC) src/trek_3dview.c -o trek_3dview $(CFLAGS) $(GL_LIBS) $(SHM_LIBS)

clean:
	rm -f trek_server trek_client trek_3dview trek_galaxy_viewer