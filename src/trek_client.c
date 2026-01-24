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
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stddef.h>
#include "network.h"
#include "shared_state.h"
#include "ui.h"

/* Colori per l'interfaccia CLI */
#define RESET   "\033[0m"
#define B_RED     "\033[1;31m"
#define B_GREEN   "\033[1;32m"
#define B_YELLOW  "\033[1;33m"
#define B_BLUE    "\033[1;34m"
#define B_MAGENTA "\033[1;35m"
#define B_CYAN    "\033[1;36m"
#define B_WHITE   "\033[1;37m"

#include <termios.h>

int sock = 0;
char captain_name[64];
int my_faction = 0;
int g_debug = 0;

#define LOG_DEBUG(...) do { if (g_debug) { printf("DEBUG: " __VA_ARGS__); fflush(stdout); } } while (0)

pid_t visualizer_pid = 0;
GameState *g_shared_state = NULL;
int shm_fd = -1;
char shm_path[64];
volatile sig_atomic_t g_visualizer_ready = 0;

/* Gestione Input Reattivo */
char g_input_buf[256] = {0};
int g_input_ptr = 0;
struct termios orig_termios;

volatile sig_atomic_t g_running = 1;

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    /* Lasciamo ISIG attivo per permettere Ctrl+C */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void reprint_prompt() {
    printf("\r\033[K" B_WHITE "%s" RESET "> Command? %s", captain_name, g_input_buf);
    fflush(stdout);
}

void handle_ack(int sig) {
    g_visualizer_ready = 1;
}

void handle_sigchld(int sig) {
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0);
}

void init_shm() {
    sprintf(shm_path, "/st_shm_%d", getpid());
    
    /* Unlink in case it already exists from a previous crash */
    shm_unlink(shm_path);
    
    shm_fd = shm_open(shm_path, O_CREAT | O_RDWR | O_EXCL, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        exit(1);
    }
    
    if (ftruncate(shm_fd, sizeof(GameState)) == -1) {
        perror("ftruncate failed");
        exit(1);
    }
    
    g_shared_state = mmap(NULL, sizeof(GameState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (g_shared_state == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }
    
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&g_shared_state->mutex, &attr);
    
    sem_init(&g_shared_state->data_ready, 1, 0);
}

void cleanup() {
    if (visualizer_pid > 0) kill(visualizer_pid, SIGTERM);
    if (g_shared_state) munmap(g_shared_state, sizeof(GameState));
    if (shm_fd != -1) {
        close(shm_fd);
        shm_unlink(shm_path);
    }
}

/* Funzione di utilità per leggere esattamente N byte dal socket */
void clear_stdin() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

int read_all(int fd, void *buf, size_t len) {
    size_t total = 0;
    char *p = (char *)buf;
    while (total < len) {
        ssize_t n = read(fd, p + total, len - total);
        if (n == 0) return 0; /* Connection closed */
        if (n < 0) {
            perror("read_all failed");
            return -1;
        }
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

void *network_listener(void *arg) {
    while (g_running) {
        int type;
        int r = read_all(sock, &type, sizeof(int));
        if (r <= 0) {
            g_running = 0;
            disable_raw_mode();
            if (r == 0) printf("\n[NET] Server closed the connection.\n");
            else printf("\n[NET] Connection lost (read error).\n");
            exit(0);
        }
        
        if (type == PKT_MESSAGE) {
            PacketMessage msg;
            msg.type = type;
            size_t fixed_size = offsetof(PacketMessage, text);
            if (read_all(sock, ((char*)&msg) + sizeof(int), fixed_size - sizeof(int)) <= 0) {
                g_running = 0;
                break;
            }
            
            if (msg.length > 0) {
                if (read_all(sock, msg.text, msg.length + 1) <= 0) {
                    g_running = 0;
                    break;
                }
            } else msg.text[0] = '\0';
            
            printf("\r\033[K"); /* Pulisce la riga di input attuale */
            if (strcmp(msg.from, "SERVER") == 0 || strcmp(msg.from, "COMPUTER") == 0 || 
                strcmp(msg.from, "SCIENCE") == 0 || strcmp(msg.from, "TACTICAL") == 0 ||
                strcmp(msg.from, "ENGINEERING") == 0 || strcmp(msg.from, "HELMSMAN") == 0 ||
                strcmp(msg.from, "WARNING") == 0 || strcmp(msg.from, "DAMAGE CONTROL") == 0) {
                printf("%s\n", msg.text);
            } else {
                printf(B_CYAN "[RADIO] %s (%s): %s\n" RESET, msg.from, 
                       (msg.faction == FACTION_FEDERATION) ? "Starfleet" : "Alien", msg.text);
            }
            reprint_prompt();
        } else if (type == PKT_UPDATE) {
            PacketUpdate upd;
            upd.type = type;
            
            /* Read fixed part up to object_count field */
            size_t fixed_size = offsetof(PacketUpdate, objects);
            if (read_all(sock, ((char*)&upd) + sizeof(int), fixed_size - sizeof(int)) <= 0) break;
            
            /* Safety check for object count to prevent buffer overflow */
            if (upd.object_count < 0 || upd.object_count > MAX_NET_OBJECTS) {
                printf("Warning: Invalid object_count received: %d\n", upd.object_count);
                break;
            }

            /* Read active objects only */
            if (upd.object_count > 0) {
                if (read_all(sock, upd.objects, upd.object_count * sizeof(NetObject)) <= 0) break;
            }
            
            if (g_shared_state) {
                if (upd.object_count > MAX_OBJECTS) upd.object_count = MAX_OBJECTS;

                pthread_mutex_lock(&g_shared_state->mutex);
                /* Sincronizziamo lo stato locale con i dati ottimizzati dal server */
                g_shared_state->shm_energy = upd.energy;
                g_shared_state->shm_crew = upd.crew_count;
                g_shared_state->shm_cargo_energy = upd.cargo_energy;
                g_shared_state->shm_cargo_torpedoes = upd.cargo_torpedoes;
                int total_s = 0;
                for(int s=0; s<6; s++) {
                    g_shared_state->shm_shields[s] = upd.shields[s];
                    total_s += upd.shields[s];
                }
                g_shared_state->is_cloaked = upd.is_cloaked;
                g_shared_state->shm_q[0] = upd.q1;
                g_shared_state->shm_q[1] = upd.q2;
                g_shared_state->shm_q[2] = upd.q3;
                sprintf(g_shared_state->quadrant, "Q-%d-%d-%d", upd.q1, upd.q2, upd.q3);

                g_shared_state->object_count = upd.object_count;
                for (int o=0; o < upd.object_count; o++) {
                    g_shared_state->objects[o].shm_x = upd.objects[o].net_x;
                    g_shared_state->objects[o].shm_y = upd.objects[o].net_y;
                    g_shared_state->objects[o].shm_z = upd.objects[o].net_z;
                    g_shared_state->objects[o].h = upd.objects[o].h;
                    g_shared_state->objects[o].m = upd.objects[o].m;
                    g_shared_state->objects[o].type = upd.objects[o].type;
                    g_shared_state->objects[o].ship_class = upd.objects[o].ship_class;
                    g_shared_state->objects[o].health_pct = upd.objects[o].health_pct;
                    g_shared_state->objects[o].id = upd.objects[o].id;
                    strncpy(g_shared_state->objects[o].shm_name, upd.objects[o].name, 63);
                    g_shared_state->objects[o].active = 1;
                }
                
                /* Append beams to shared state (Queue logic) */
                if (upd.beam_count > 0) {
                    for (int b=0; b < upd.beam_count; b++) {
                        if (g_shared_state->beam_count < MAX_BEAMS) {
                            int idx = g_shared_state->beam_count;
                            g_shared_state->beams[idx].shm_tx = upd.beams[b].net_tx;
                            g_shared_state->beams[idx].shm_ty = upd.beams[b].net_ty;
                            g_shared_state->beams[idx].shm_tz = upd.beams[b].net_tz;
                            g_shared_state->beams[idx].active = upd.beams[b].active;
                            g_shared_state->beam_count++;
                        }
                    }
                }
                
                /* Projectile position */
                g_shared_state->torp.shm_x = upd.torp.net_x;
                g_shared_state->torp.shm_y = upd.torp.net_y;
                g_shared_state->torp.shm_z = upd.torp.net_z;
                g_shared_state->torp.active = upd.torp.active;
                
                /* Event Latching (Visualizer will clear these) */
                if (upd.boom.active) {
                    g_shared_state->boom.shm_x = upd.boom.net_x;
                    g_shared_state->boom.shm_y = upd.boom.net_y;
                    g_shared_state->boom.shm_z = upd.boom.net_z;
                    g_shared_state->boom.active = 1;
                }
                
                if (upd.dismantle.active) {
                    g_shared_state->dismantle.shm_x = upd.dismantle.net_x;
                    g_shared_state->dismantle.shm_y = upd.dismantle.net_y;
                    g_shared_state->dismantle.shm_z = upd.dismantle.net_z;
                    g_shared_state->dismantle.species = upd.dismantle.species;
                    g_shared_state->dismantle.active = 1;
                }
                
                g_shared_state->frame_id++; 
                pthread_mutex_unlock(&g_shared_state->mutex);
                sem_post(&g_shared_state->data_ready);
            }
        }
    }
    return NULL;
}

void handle_sigint(int sig) {
    exit(0);
}

int main(int argc, char *argv[]) {
    struct sockaddr_in serv_addr;
    char server_ip[64];
    int my_ship_class = SHIP_CLASS_GENERIC_ALIEN;
    signal(SIGPIPE, SIG_IGN);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) g_debug = 1;
    }

    struct sigaction sa;
    sa.sa_handler = handle_ack;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR2, &sa, NULL);

    struct sigaction sa_exit;
    sa_exit.sa_handler = handle_sigint;
    sigemptyset(&sa_exit.sa_mask);
    sa_exit.sa_flags = 0;
    sigaction(SIGINT, &sa_exit, NULL);
    sigaction(SIGTERM, &sa_exit, NULL);

    struct sigaction sa_chld;
    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);

    atexit(cleanup);

    printf(B_YELLOW "--- TREK ULTRA CLIENT ---\n" RESET);
    LOG_DEBUG("sizeof(StarTrekGame) = %zu\n", sizeof(StarTrekGame));
    LOG_DEBUG("sizeof(PacketUpdate) = %zu\n", sizeof(PacketUpdate));
    printf("Server IP: "); scanf("%s", server_ip);
    printf("Commander Name: "); scanf("%s", captain_name);
    clear_stdin();

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(DEFAULT_PORT);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }

    /* Identity Check */
    PacketLogin qpkt;
    memset(&qpkt, 0, sizeof(PacketLogin));
    qpkt.type = PKT_QUERY;
    strcpy(qpkt.name, captain_name);
    write_all(sock, &qpkt, sizeof(PacketLogin));
    
    int is_known = 0;
    read_all(sock, &is_known, sizeof(int));

    if (!is_known) {
        printf("\n" B_WHITE "--- NEW RECRUIT IDENTIFIED ---" RESET "\n");
        printf("--- SELECT YOUR FACTION ---\n");
        printf(" 0: Federation\n 1: Klingon\n 2: Romulan\n 3: Borg\n 4: Cardassian\n 5: Jem'Hadar\n 6: Tholian\n 7: Gorn\n 8: Ferengi\n 9: Species 8472\n 10: Breen\n 11: Hirogen\nSelection: ");
        scanf("%d", &my_faction);
        
        if (my_faction == FACTION_FEDERATION) {
            printf("\n" B_WHITE "--- SELECT YOUR CLASS ---" RESET "\n");
            printf(" 0: Constitution\n 1: Miranda\n 2: Excelsior\n 3: Constellation\n 4: Defiant\n 5: Galaxy\n 6: Sovereign\n 7: Intrepid\n 8: Akira\n 9: Nebula\n 10: Ambassador\n 11: Oberth\n 12: Steamrunner\nSelection: ");
            scanf("%d", &my_ship_class);
        }
        clear_stdin();
    } else {
        printf(B_CYAN "\n--- RETURNING CAPTAIN RECOGNIZED ---\n" RESET);
    }

    /* Final Login */
    PacketLogin lpkt;
    memset(&lpkt, 0, sizeof(PacketLogin));
    lpkt.type = PKT_LOGIN;
    strcpy(lpkt.name, captain_name);
    lpkt.faction = my_faction;
    lpkt.ship_class = my_ship_class;
    
    LOG_DEBUG("Sending login packet (%zu bytes)...\n", sizeof(PacketLogin));
    write_all(sock, &lpkt, sizeof(PacketLogin));

    /* Ricezione Galassia Master (Sincronizzazione iniziale) */
    StarTrekGame master_sync;
    printf("Synchronizing with Galaxy Server...\n");
    LOG_DEBUG("Waiting for Galaxy Master (%zu bytes)...\n", sizeof(StarTrekGame));
    if (read_all(sock, &master_sync, sizeof(StarTrekGame)) == sizeof(StarTrekGame)) {
        printf(B_GREEN "Galaxy Map synchronized.\n" RESET);
    } else {
        printf(B_RED "ERROR: Failed to synchronize Galaxy Map.\n" RESET);
    }

    init_shm();
    
    /* Copy Galaxy Master to SHM for 3D Map View */
    if (g_shared_state) {
        pthread_mutex_lock(&g_shared_state->mutex);
        memcpy(g_shared_state->shm_galaxy, master_sync.g, sizeof(master_sync.g));
        pthread_mutex_unlock(&g_shared_state->mutex);
    }
    
    if (getenv("DISPLAY") == NULL) {
        printf(B_RED "WARNING: No DISPLAY detected. 3D View might not start.\n" RESET);
    }

    visualizer_pid = fork();
    if (visualizer_pid == -1) {
        perror("fork failed");
        exit(1);
    }
    if (visualizer_pid == 0) {
        /* Child process */
        if (execl("./trek_3dview", "trek_3dview", shm_path, NULL) == -1) {
            perror("execl failed to start ./trek_3dview");
            _exit(1);
        }
    }

    /* Wait for visualizer handshake with timeout (5 seconds) */
    printf("Waiting for Tactical View initialization...\n");
    int timeout = 500; /* 5 seconds */
    while(!g_visualizer_ready && timeout-- > 0) {
        /* Check if child is still alive */
        int status;
        if (waitpid(visualizer_pid, &status, WNOHANG) != 0) {
            printf(B_RED "ERROR: Tactical View process terminated unexpectedly.\n" RESET);
            break;
        }
        usleep(10000);
    }
    
    if (g_visualizer_ready) {
        printf(B_GREEN "Tactical View (3D) initialized.\n" RESET);
    } else {
        printf(B_RED "WARNING: Tactical View timed out. Proceeding in CLI-only mode.\n" RESET);
    }

    /* Thread per ascoltare il server */
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, network_listener, NULL);

    printf(B_GREEN "Connected to Galaxy Server. Command Deck ready.\n" RESET);
    enable_raw_mode();
    reprint_prompt();

    while (g_running) {
        char c;
        if (read(STDIN_FILENO, &c, 1) > 0) {
            if (c == '\n' || c == '\r') {
                if (g_input_ptr > 0) {
                    printf("\n");
                    g_input_buf[g_input_ptr] = 0;
                    
                    if (strcmp(g_input_buf, "xxx") == 0) {
                        PacketCommand cpkt = {PKT_COMMAND, "xxx"};
                        send(sock, &cpkt, sizeof(cpkt), 0);
                        g_running = 0;
                        disable_raw_mode();
                        exit(0);
                    }
                    if (strcmp(g_input_buf, "help") == 0) {
                        printf(B_WHITE "\n--- STAR TREK ULTRA: MULTIPLAYER COMMANDS ---" RESET "\n");
                        printf("nav H M W   : Warp Navigation (Heading 0-359, Mark -90/90, Warp 0-8)\n");
                        printf("imp H M S   : Impulse Drive (H, M, Speed 0.0-1.0). imp 0 0 0 to stop.\n");
                        printf("srs         : Short Range Sensors (Current Quadrant View)\n");
                        printf("lrs         : Long Range Sensors (3x3x3 Neighborhood Scan)\n");
                        printf("pha E       : Fire Phasers (Distance-based damage, uses Energy)\n");
                        printf("tor H M     : Launch Photon Torpedo (Ballistic projectile)\n");
                        printf("she F R T B L RI : Configure 6 Shield Quadrants\n");
                        printf("lock ID     : Lock-on Target (0:Self, 1+:Nearby vessels)\n");
                        printf("pow E S W   : Power Distribution (Engines, Shields, Weapons %%)\n");
                        printf("psy         : Psychological Warfare (Corbomite Bluff)\n");
                        printf("aux probe QX QY QZ: Launch long-range probe\n");
                        printf("aux jettison: Eject Warp Core (Suicide maneuver)\n");
                        printf("bor         : Boarding party operation (Dist < 1.0)\n");
                        printf("min         : Planetary Mining (Must be in orbit dist < 2.0)\n");
                        printf("doc         : Dock with Starbase (Replenish/Repair, same faction)\n");
                        printf("con T A     : Convert Resources (1:Dilithium->E, 3:Verterium->Torps)\n");
                        printf("load T A    : Load from Cargo Bay (1:Energy, 2:Torps)\n");
                        printf("rep ID      : Repair System (Uses Tritanium or Isolinear Crystals)\n");
                        printf("inv         : Cargo Inventory Report\n");
                        printf("who         : List active captains in galaxy\n");
                        printf("cal QX QY QZ: Navigation Calculator\n");
                        printf("apr ID DIST : Approach target autopilot\n");
                        printf("cha         : Chase locked target (Inter-sector aware)\n");
                        printf("sco         : Solar scooping for energy\n");
                        printf("har         : Antimatter harvest from Black Hole\n");
                        printf("sta         : Mission Status Report\n");
                        printf("dam         : Detailed Damage Report\n");
                        printf("rad MSG     : Send Global Radio Message\n");
                        printf("rad @Fac MSG: Send to Faction (e.g. @Romulan ...)\n");
                        printf("rad #ID MSG : Send Private Message to Player ID\n");
                        printf("clo         : Toggle Cloaking Device (Consumes constant Energy)\n");
                        printf("axs / grd   : Toggle 3D Visual Guides\n");
                        printf("xxx         : Self-Destruct\n");
                    } else if (strcmp(g_input_buf, "axs") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_show_axes = !g_shared_state->shm_show_axes;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                            printf("Axes toggled.\n");
                        }
                    } else if (strcmp(g_input_buf, "grd") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_show_grid = !g_shared_state->shm_show_grid;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                            printf("Grid toggled.\n");
                        }
                    } else if (strcmp(g_input_buf, "map") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_show_map = !g_shared_state->shm_show_map;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                            printf("Starmap toggled.\n");
                        }
                    } else if (strncmp(g_input_buf, "rad ", 4) == 0) {
                        PacketMessage mpkt;
                        memset(&mpkt, 0, sizeof(PacketMessage));
                        mpkt.type = PKT_MESSAGE;
                        strcpy(mpkt.from, captain_name);
                        mpkt.faction = my_faction;
                        mpkt.scope = SCOPE_GLOBAL;
                        
                        char *msg_start = g_input_buf + 4;
                        if (msg_start[0] == '@') {
                            char target_name[64];
                            int offset = 0;
                            sscanf(msg_start + 1, "%s%n", target_name, &offset);
                            if (offset > 0) {
                                mpkt.scope = SCOPE_FACTION;
                                if (strcasecmp(target_name, "Federation")==0 || strcasecmp(target_name, "Fed")==0) mpkt.faction = FACTION_FEDERATION;
                                else if (strcasecmp(target_name, "Klingon")==0 || strcasecmp(target_name, "Kli")==0) mpkt.faction = FACTION_KLINGON;
                                else if (strcasecmp(target_name, "Romulan")==0 || strcasecmp(target_name, "Rom")==0) mpkt.faction = FACTION_ROMULAN;
                                else if (strcasecmp(target_name, "Borg")==0 || strcasecmp(target_name, "Bor")==0) mpkt.faction = FACTION_BORG;
                                else if (strcasecmp(target_name, "Cardassian")==0 || strcasecmp(target_name, "Car")==0) mpkt.faction = FACTION_CARDASSIAN;
                                else if (strcasecmp(target_name, "JemHadar")==0 || strcasecmp(target_name, "Jem")==0) mpkt.faction = FACTION_JEM_HADAR;
                                else if (strcasecmp(target_name, "Tholian")==0 || strcasecmp(target_name, "Tho")==0) mpkt.faction = FACTION_THOLIAN;
                                else if (strcasecmp(target_name, "Gorn")==0) mpkt.faction = FACTION_GORN;
                                else if (strcasecmp(target_name, "Ferengi")==0 || strcasecmp(target_name, "Fer")==0) mpkt.faction = FACTION_FERENGI;
                                else if (strcasecmp(target_name, "Species8472")==0 || strcasecmp(target_name, "8472")==0) mpkt.faction = FACTION_SPECIES_8472;
                                else if (strcasecmp(target_name, "Breen")==0) mpkt.faction = FACTION_BREEN;
                                else if (strcasecmp(target_name, "Hirogen")==0) mpkt.faction = FACTION_HIROGEN;
                                else {
                                    mpkt.scope = SCOPE_GLOBAL; /* Fallback */
                                }
                                if (strlen(msg_start) > (1 + offset + 1))
                                    strncpy(mpkt.text, msg_start + 1 + offset + 1, 4095);
                                else 
                                    mpkt.text[0] = '\0';
                            } else strncpy(mpkt.text, msg_start, 4095);
                        } else if (msg_start[0] == '#') {
                            int tid;
                            int offset = 0;
                            if (sscanf(msg_start + 1, "%d%n", &tid, &offset) == 1) {
                                mpkt.scope = SCOPE_PRIVATE;
                                mpkt.target_id = tid;
                                if (strlen(msg_start) > (1 + offset + 1))
                                    strncpy(mpkt.text, msg_start + 1 + offset + 1, 4095);
                                else 
                                    mpkt.text[0] = '\0';
                            } else strncpy(mpkt.text, msg_start, 4095);
                        } else {
                            strncpy(mpkt.text, msg_start, 4095);
                        }
                        
                        mpkt.length = strlen(mpkt.text);
                        size_t pkt_size = offsetof(PacketMessage, text) + mpkt.length + 1;
                        if (pkt_size > sizeof(PacketMessage)) pkt_size = sizeof(PacketMessage);
                        
                        size_t sent_msg = 0;
                        char *p_msg = (char *)&mpkt;
                        while (sent_msg < pkt_size) {
                            ssize_t n = send(sock, p_msg + sent_msg, pkt_size - sent_msg, 0);
                            if (n <= 0) break;
                            sent_msg += n;
                        }
                    } else {
                        PacketCommand cpkt = {PKT_COMMAND, ""};
                        strncpy(cpkt.cmd, g_input_buf, 255);
                        send(sock, &cpkt, sizeof(cpkt), 0);
                    }
                    
                    g_input_ptr = 0;
                    g_input_buf[0] = 0;
                } else {
                    printf("\n");
                }
                reprint_prompt();
            } else if (c == 127 || c == 8) { /* Backspace */
                if (g_input_ptr > 0) {
                    g_input_ptr--;
                    g_input_buf[g_input_ptr] = 0;
                    reprint_prompt();
                }
            } else if (c >= 32 && c <= 126 && g_input_ptr < 255) {
                g_input_buf[g_input_ptr++] = c;
                g_input_buf[g_input_ptr] = 0;
                reprint_prompt();
            } else if (c == 27) { /* ESC o sequenze speciali */
                /* Potremmo gestire le frecce qui, ma per ora lo ignoriamo */
            }
        }
    }

    close(sock);
    return 0;
}
