/* 
 * STARTREK ULTRA - 3D LOGIC ENGINE 
 * Authors: Nicola Taibi, Supported by Google Gemini
 * Copyright (C) 2026 Nicola Taibi
 * License: GNU General Public License v3.0
 */

#define _DEFAULT_SOURCE
#define GL_GLEXT_PROTOTYPES
#include <GL/freeglut.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <signal.h>
#include <locale.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include "shared_state.h"

#define IS_Q_VALID(q1,q2,q3) ((q1)>=1 && (q1)<=10 && (q2)>=1 && (q2)<=10 && (q3)>=1 && (q3)<=10)

#include "network.h"

/* VBO Globals */
GLuint vbo_stars = 0;
GLuint vbo_grid = 0;
int grid_vertex_count = 0;

/* Shader Globals */
GLuint starShaderProgram = 0;
GLuint bhShaderProgram = 0;
GLuint whShaderProgram = 0;

GLuint compileShader(const char* source, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        fprintf(stderr, "Shader Compilation Error: %s\n", infoLog);
    }
    return shader;
}

GLuint linkProgram(GLuint vert, GLuint frag) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);
    int success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        fprintf(stderr, "Shader Linking Error: %s\n", infoLog);
    }
    return program;
}

void initShaders() {
    const char* starVert = "#version 120\n"
        "void main() { gl_Position = ftransform(); gl_FrontColor = gl_Color; }";
    const char* starFrag = "#version 120\n"
        "uniform float time;\n"
        "void main() { float p = (sin(time * 3.0) + 1.0) * 0.5;\n"
        "gl_FragColor = vec4(gl_Color.rgb, gl_Color.a * (0.6 + p * 0.4)); }";

    GLuint sv = compileShader(starVert, GL_VERTEX_SHADER);
    GLuint sf = compileShader(starFrag, GL_FRAGMENT_SHADER);
    starShaderProgram = linkProgram(sv, sf);

    const char* bhFrag = "#version 120\n"
        "uniform float time;\n"
        "varying vec3 pos;\n"
        "void main() {\n"
        "  float d = length(pos);\n"
        "  float ripple = sin(d * 20.0 - time * 10.0) * 0.5 + 0.5;\n"
        "  vec3 col = vec3(0.4, 0.0, 0.8) * (1.0 - d) + vec3(0.1, 0.0, 0.2);\n"
        "  gl_FragColor = vec4(col * ripple, 0.7);\n"
        "}";
    
    const char* bhVert = "#version 120\n"
        "varying vec3 pos;\n"
        "void main() {\n"
        "  pos = gl_Vertex.xyz;\n"
        "  gl_Position = ftransform();\n"
        "}";
    bhShaderProgram = linkProgram(compileShader(bhVert, GL_VERTEX_SHADER), compileShader(bhFrag, GL_FRAGMENT_SHADER));

    /* Wormhole Shader (Cyan/Blue Variant) */
    const char* whFrag = "#version 120\n"
        "uniform float time;\n"
        "varying vec3 pos;\n"
        "void main() {\n"
        "  float d = length(pos);\n"
        "  float ripple = sin(d * 30.0 - time * 20.0) * 0.5 + 0.5;\n"
        "  vec3 col = vec3(0.0, 0.8, 1.0) * (1.0 - d) + vec3(0.0, 0.1, 0.3);\n"
        "  gl_FragColor = vec4(col * ripple + vec3(0.8, 0.9, 1.0) * pow(ripple, 4.0), 0.8);\n"
        "}";
    whShaderProgram = linkProgram(compileShader(bhVert, GL_VERTEX_SHADER), compileShader(whFrag, GL_FRAGMENT_SHADER));
    
    /* Advanced Ship Shader */
}

int shm_fd = -1;
GameState *g_shared_state = NULL;

volatile int g_data_dirty = 0;

void *shm_listener_thread(void *arg) {
    while(1) {
        if (g_shared_state) {
            sem_wait(&g_shared_state->data_ready);
            g_data_dirty = 1;
        } else usleep(10000);
    }
    return NULL;
}
volatile int g_is_loading = 0;
float angleY = 0.0f;
float angleX = 20.0f;
float zoom = -14.0f;
float autoRotate = 0.075f;
float pulse = 0.0f;
float map_anim = 0.0f;

int g_energy = 0, g_crew = 0, g_shields = 0, g_klingons = 0;
int g_duranium_plating = 0;
float g_hull_integrity = 100.0f;
int g_shields_val[6] = {0};
int g_cargo_energy = 0, g_cargo_torps = 0, g_torpedoes_launcher = 0;
float g_system_health[10] = {0};
int g_inventory[8] = {0};
int g_lock_target = 0;
int g_show_axes = 0;
int g_show_grid = 0;
int g_show_map = 0;
int g_my_q[3] = {1,1,1};
int64_t g_galaxy[11][11][11];
int g_show_hud = 1; /* Default HUD ON */
char g_quadrant[128] = "Scanning...";
char g_last_quadrant[128] = "";
char g_player_name[64] = "Unknown";
int g_player_class = 0;

#define MAX_TRAIL 40
typedef struct {
    float x, y, z;
    float tx, ty, tz; /* Interpolation targets */
    float h, m;
    float th, tm;     /* Target heading and mark */
    int type;
    int ship_class;
    int health_pct;   /* HUD */
    int energy;       /* HUD */
    int plating;      /* HUD */
    int hull_integrity; /* HUD */
    int faction;      /* HUD */
    int id;           /* HUD */
    char name[64];    /* HUD Name */
    float trail[MAX_TRAIL][3];
    int trail_ptr;
    int trail_count;
    double last_update_time;
} GameObject;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float r, g, b;
    int active;
} Particle;

typedef struct {
    float x, y, z;
    int species;
    int timer;
    Particle particles[100];
} Dismantle;

typedef struct {
    float x, y, z;
    int timer;
    Particle particles[150];
} ArrivalEffect;

GameObject objects[200];
int objectCount = 0;
Dismantle g_dismantle = {-100, -100, -100, 0, 0};
ArrivalEffect g_arrival_fx = {0, 0, 0, 0};

/* Rimosse variabili globali scia singola per scia universale */
typedef struct { float x, y, z, alpha; } PhaserBeam;
PhaserBeam beams[10];
int beamCount = 0;

typedef struct { float x, y, z, h, m; int active; int timer; } ViewPoint;
ViewPoint g_torp = {0,0,0,0,0};
ViewPoint g_boom = {0,0,0,0,0};
ViewPoint g_wormhole = {0,0,0,0,0};
ViewPoint g_jump_arrival = {0,0,0,0,0};
ViewPoint g_sn_pos = {0,0,0,0,0};
int g_sn_q[3] = {0,0,0};

float enterpriseX = 0, enterpriseY = 0, enterpriseZ = 0;

float stars[1000][3];

/* Prototypes */
void drawStarbase(float x, float y, float z);
void drawPlanet(float x, float y, float z);
void drawGrid();
void drawKlingon(float x, float y, float z);
void drawRomulan(float x, float y, float z);
void drawBorg(float x, float y, float z);
void drawCardassian(float x, float y, float z);
void drawJemHadar(float x, float y, float z);
void drawTholian(float x, float y, float z);
void drawGorn(float x, float y, float z);
void drawFerengi(float x, float y, float z);
void drawSpecies8472(float x, float y, float z);
void drawBreen(float x, float y, float z);
void drawHirogen(float x, float y, float z);
void drawNacelle(float len, float width, float r, float g, float b);
void drawDeflector(float r, float g, float b);
void drawConstitution();
void drawMiranda();
void drawExcelsior();
void drawConstellation();
void drawDefiant();
void drawGalaxy();
void drawSovereign();
void drawIntrepid();
void drawAkira();
void drawNebulaShip();
void drawAmbassador();
void drawOberth();
void drawSteamrunner();
void drawGlow(float radius, float r, float g, float b, float alpha);
void drawHullDetail(void (*drawFunc)(void), float r, float g, float b);
void drawNavLights(float x, float y, float z);
void drawStarfleetSaucer(float sx, float sy, float sz);
void glutSolidSphere_wrapper_saucer();
void glutSolidSphere_wrapper_hull();
void glutSolidSphere_wrapper_defiant();
void glutSolidCube_wrapper();

void handle_signal(int sig) { if (sig == SIGUSR1) g_data_dirty = 1; }

void initStars() {
    for(int i=0; i<1000; i++) {
        float r = 150.0f + (float)(rand()%100);
        float t = (float)(rand()%360) * 3.14/180;
        float p = (float)(rand()%360) * 3.14/180;
        stars[i][0] = r * sin(p) * cos(t);
        stars[i][1] = r * sin(p) * sin(t);
        stars[i][2] = r * cos(p);
    }
}

void initVBOs() {
    glGenBuffers(1, &vbo_stars);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_stars);
    glBufferData(GL_ARRAY_BUFFER, sizeof(stars), stars, GL_STATIC_DRAW);
    int max_verts = 11 * 11 * 3 * 2; 
    float *grid_data = malloc(max_verts * 3 * sizeof(float));
    int idx = 0;
    for(int i=0; i<=10; i++) {
        float p = -5.0f + (float)i;
        for(int j=0; j<=10; j++) {
            float q = -5.0f + (float)j;
            grid_data[idx++] = p; grid_data[idx++] = q; grid_data[idx++] = -5.0f;
            grid_data[idx++] = p; grid_data[idx++] = q; grid_data[idx++] = 5.0f;
            grid_data[idx++] = p; grid_data[idx++] = -5.0f; grid_data[idx++] = q;
            grid_data[idx++] = p; grid_data[idx++] = 5.0f; grid_data[idx++] = q;
            grid_data[idx++] = -5.0f; grid_data[idx++] = p; grid_data[idx++] = q;
            grid_data[idx++] = 5.0f; grid_data[idx++] = p; grid_data[idx++] = q;
        }
    }
    grid_vertex_count = idx / 3;
    glGenBuffers(1, &vbo_grid);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_grid);
    glBufferData(GL_ARRAY_BUFFER, idx * sizeof(float), grid_data, GL_STATIC_DRAW);
    free(grid_data);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

long long last_frame_id = -1;

void loadGameState() {
    if (!g_shared_state) return;
    pthread_mutex_lock(&g_shared_state->mutex);
    if (g_shared_state->frame_id == last_frame_id) { pthread_mutex_unlock(&g_shared_state->mutex); return; }
    last_frame_id = g_shared_state->frame_id;
    g_is_loading = 1;
    g_energy = g_shared_state->shm_energy;
    g_duranium_plating = g_shared_state->shm_duranium_plating;
    g_hull_integrity = g_shared_state->shm_hull_integrity;
    g_crew = g_shared_state->shm_crew;
    g_torpedoes_launcher = g_shared_state->shm_torpedoes;
    g_cargo_energy = g_shared_state->shm_cargo_energy;
    g_cargo_torps = g_shared_state->shm_cargo_torpedoes;
    for(int s=0; s<10; s++) g_system_health[s] = g_shared_state->shm_system_health[s];
    for(int inv=0; inv<8; inv++) g_inventory[inv] = g_shared_state->inventory[inv];
    g_lock_target = g_shared_state->shm_lock_target;
    int total_s = 0;
    for(int s=0; s<6; s++) total_s += g_shared_state->shm_shields[s];
    g_shields = total_s / 6;
    for(int s=0; s<6; s++) g_shields_val[s] = g_shared_state->shm_shields[s];
    g_klingons = g_shared_state->klingons;
    strncpy(g_player_name, g_shared_state->objects[0].shm_name, 63);
    g_player_class = g_shared_state->objects[0].ship_class;
    
    int quadrant_changed = 0;
    if (strcmp(g_quadrant, g_shared_state->quadrant) != 0) {
        quadrant_changed = 1;
        strcpy(g_quadrant, g_shared_state->quadrant);
        /* Cleanup local visual effects on sector jump */
        g_wormhole.active = 0;
        g_jump_arrival.timer = 0;
    }
    
    g_show_axes = g_shared_state->shm_show_axes;
    g_show_grid = g_shared_state->shm_show_grid;
    g_show_map = g_shared_state->shm_show_map;
    g_my_q[0] = g_shared_state->shm_q[0];
    g_my_q[1] = g_shared_state->shm_q[1];
    g_my_q[2] = g_shared_state->shm_q[2];
    
    /* Center the camera/world on the player's explicit sector coordinates */
    float s0 = g_shared_state->shm_s[0];
    float s1 = g_shared_state->shm_s[1];
    float s2 = g_shared_state->shm_s[2];
    
    if (!isnan(s0) && !isnan(s1) && !isnan(s2)) {
        enterpriseX = s0 - 5.0f;
        enterpriseY = s2 - 5.0f; /* Y in Trek is Z in Viewer */
        enterpriseZ = 5.0f - s1; /* Z in Trek is -Y in Viewer */
    }

    memcpy(g_galaxy, g_shared_state->shm_galaxy, sizeof(g_galaxy));

    int updated[200] = {0};
    objectCount = g_shared_state->object_count;
    for(int i=0; i<objectCount; i++) {
        int target_id = g_shared_state->objects[i].id;
        int local_idx = -1;

        /* Find existing object by ID */
        for(int k=0; k<200; k++) {
            if (objects[k].id == target_id && target_id != 0) {
                local_idx = k;
                break;
            }
        }

        /* If not found, find an empty slot */
        if (local_idx == -1) {
            for(int k=0; k<200; k++) {
                if (objects[k].id == 0) {
                    local_idx = k;
                    objects[k].id = target_id;
                    /* First time initialization */
                    objects[k].x = -100.0f; 
                    break;
                }
            }
        }

        if (local_idx != -1) {
            updated[local_idx] = 1;
            GameObject *obj = &objects[local_idx];
            float next_x = g_shared_state->objects[i].shm_x - 5.0f;
            float next_y = g_shared_state->objects[i].shm_z - 5.0f;
            float next_z = 5.0f - g_shared_state->objects[i].shm_y;
            
            if (isnan(next_x) || isnan(next_y) || isnan(next_z)) {
                updated[local_idx] = 0; /* Ignore this object */
                continue;
            }

            float dx = next_x - obj->x;
            float dy = next_y - obj->y;
            float dz = next_z - obj->z;
            
            if (quadrant_changed || obj->x < -50.0f || (dx*dx + dy*dy + dz*dz) > 25.0f) {
                obj->x = obj->tx = next_x;
                obj->y = obj->ty = next_y;
                obj->z = obj->tz = next_z;
                obj->h = obj->th = g_shared_state->objects[i].h;
                obj->m = obj->tm = g_shared_state->objects[i].m;
                obj->trail_count = 0;
                obj->trail_ptr = 0;
            } else {
                obj->tx = next_x;
                obj->ty = next_y;
                obj->tz = next_z;
            }

            obj->th = g_shared_state->objects[i].h;
            obj->tm = g_shared_state->objects[i].m;
            obj->last_update_time = glutGet(GLUT_ELAPSED_TIME);
            obj->type = g_shared_state->objects[i].type;
            obj->ship_class = g_shared_state->objects[i].ship_class;
            obj->health_pct = g_shared_state->objects[i].health_pct;
            obj->energy = g_shared_state->objects[i].energy;
            obj->plating = g_shared_state->objects[i].plating;
            obj->hull_integrity = g_shared_state->objects[i].hull_integrity;
            obj->faction = g_shared_state->objects[i].faction;
            strncpy(obj->name, g_shared_state->objects[i].shm_name, 63);
        }
    }

    /* Clear stale objects not present in the latest update */
    for(int k=0; k<200; k++) {
        if (!updated[k]) {
            objects[k].type = 0;
            objects[k].id = 0;
        }
    }

    if (g_shared_state->beam_count > 0) {
        for(int i=0; i<g_shared_state->beam_count; i++) {
            int slot = -1;
            for(int j=0; j<10; j++) if(beams[j].alpha <= 0) { slot = j; break; }
            if (slot == -1) slot = rand()%10;
            beams[slot].x = g_shared_state->beams[i].shm_tx - 5.0f;
            beams[slot].y = g_shared_state->beams[i].shm_tz - 5.0f;
            beams[slot].z = 5.0f - g_shared_state->beams[i].shm_ty;
            beams[slot].alpha = 1.5f;
        }
        /* Consume events */
        g_shared_state->beam_count = 0;
    }
    if (g_shared_state->torp.active) {
        g_torp.x = g_shared_state->torp.shm_x - 5.0f;
        g_torp.y = g_shared_state->torp.shm_z - 5.0f;
        g_torp.z = 5.0f - g_shared_state->torp.shm_y;
        g_torp.active = 1;
    } else g_torp.active = 0;
    if (g_shared_state->boom.active) {
        g_boom.x = g_shared_state->boom.shm_x - 5.0f;
        g_boom.y = g_shared_state->boom.shm_z - 5.0f;
        g_boom.z = 5.0f - g_shared_state->boom.shm_y;
        g_boom.active = 1;
        g_boom.timer = 40; /* 1.3 seconds approx */
        /* Consume event */
        g_shared_state->boom.active = 0;
    }
    if (g_shared_state->wormhole.active) {
        g_wormhole.x = g_shared_state->wormhole.shm_x - 5.0f;
        g_wormhole.y = g_shared_state->wormhole.shm_z - 5.0f;
        g_wormhole.z = 5.0f - g_shared_state->wormhole.shm_y;
        g_wormhole.h = 0; g_wormhole.m = 0;
        g_wormhole.active = 1;
    } else {
        g_wormhole.active = 0;
    }

    if (g_shared_state->jump_arrival.active) {
        float jx = g_shared_state->jump_arrival.shm_x - 5.0f;
        float jy = g_shared_state->jump_arrival.shm_z - 5.0f;
        float jz = 5.0f - g_shared_state->jump_arrival.shm_y;
        
        /* Sanity check: prevent invalid arrival triggers (large values indicate uninitialized state) */
        if (fabs(jx) > 50.0f || fabs(jy) > 50.0f) {
             g_shared_state->jump_arrival.active = 0;
        } else {
            g_jump_arrival.x = jx; g_jump_arrival.y = jy; g_jump_arrival.z = jz;
            g_jump_arrival.h = 0; g_jump_arrival.m = 0;
            g_jump_arrival.active = 1;
            g_jump_arrival.timer = 300;
            g_arrival_fx.x = jx; g_arrival_fx.y = jy; g_arrival_fx.z = jz;
            g_arrival_fx.timer = 300;
            
            for(int i=0; i<150; i++) {
                float theta = (rand() % 360) * M_PI / 180.0f;
                float phi = (rand() % 180 - 90) * M_PI / 180.0f;
                float dist = 3.0f + (rand() % 200) / 100.0f;
                g_arrival_fx.particles[i].x = g_arrival_fx.x + dist * cos(phi) * cos(theta);
                g_arrival_fx.particles[i].y = g_arrival_fx.y + dist * sin(phi);
                g_arrival_fx.particles[i].z = g_arrival_fx.z + dist * cos(phi) * sin(theta);
                g_arrival_fx.particles[i].vx = (g_arrival_fx.x - g_arrival_fx.particles[i].x) / 100.0f;
                g_arrival_fx.particles[i].vy = (g_arrival_fx.y - g_arrival_fx.particles[i].y) / 100.0f;
                g_arrival_fx.particles[i].vz = (g_arrival_fx.z - g_arrival_fx.particles[i].z) / 100.0f;
                g_arrival_fx.particles[i].r = (rand() % 100) / 100.0f;
                g_arrival_fx.particles[i].g = (rand() % 100) / 100.0f;
                g_arrival_fx.particles[i].b = (rand() % 100) / 100.0f;
                g_arrival_fx.particles[i].active = 1;
            }
            g_shared_state->jump_arrival.active = 0;
        }
    }

    /* Supernova epicenter */
    if (g_shared_state->supernova_pos.active > 0) {
        g_sn_pos.x = g_shared_state->supernova_pos.shm_x - 5.0f;
        g_sn_pos.y = g_shared_state->supernova_pos.shm_z - 5.0f;
        g_sn_pos.z = 5.0f - g_shared_state->supernova_pos.shm_y;
        g_sn_pos.active = 1;
        g_sn_pos.timer = g_shared_state->supernova_pos.active;
        g_sn_q[0] = g_shared_state->shm_sn_q[0];
        g_sn_q[1] = g_shared_state->shm_sn_q[1];
        g_sn_q[2] = g_shared_state->shm_sn_q[2];
    } else {
        g_sn_pos.active = 0;
        g_sn_q[0] = g_sn_q[1] = g_sn_q[2] = 0;
    }

    /* Dismantle */
    if (g_shared_state->dismantle.active) {
        g_dismantle.x = g_shared_state->dismantle.shm_x - 5.0f;
        g_dismantle.y = g_shared_state->dismantle.shm_z - 5.0f;
        g_dismantle.z = 5.0f - g_shared_state->dismantle.shm_y;
        g_dismantle.species = g_shared_state->dismantle.species;
        g_dismantle.timer = 60;
        for(int i=0; i<100; i++) {
            g_dismantle.particles[i].x = g_dismantle.x;
            g_dismantle.particles[i].y = g_dismantle.y;
            g_dismantle.particles[i].z = g_dismantle.z;
            g_dismantle.particles[i].vx = ((rand()%100)-50)/150.0f;
            g_dismantle.particles[i].vy = ((rand()%100)-50)/150.0f;
            g_dismantle.particles[i].vz = ((rand()%100)-50)/150.0f;
            g_dismantle.particles[i].r = (rand()%100)/100.0f;
            g_dismantle.particles[i].g = (rand()%100)/100.0f;
            g_dismantle.particles[i].b = (rand()%100)/100.0f;
            g_dismantle.particles[i].active = 1;
        }
        /* Reset event to prevent re-triggering */
        g_shared_state->dismantle.active = 0;
    }
    pthread_mutex_unlock(&g_shared_state->mutex);
    kill(getppid(), SIGUSR2);
    g_is_loading = 0;
}

void drawText3D(float x, float y, float z, const char* text) {
    glRasterPos3f(x, y, z);
    for(int i=0; i<strlen(text); i++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, text[i]);
}

const char* getSpeciesName(int s) {
    switch(s) {
        case 1: return "Player"; case 3: return "Starbase"; case 4: return "Star"; case 5: return "Planet"; case 6: return "Black Hole";
        case 7: return "Nebula"; case 8: return "Pulsar";
        case 10: return "Klingon"; case 11: return "Romulan"; case 12: return "Borg";
        case 13: return "Cardassian"; case 14: return "Jem'Hadar"; case 15: return "Tholian";
        case 16: return "Gorn"; case 17: return "Ferengi"; case 18: return "Species 8472";
        case 19: return "Breen"; case 20: return "Hirogen";
        default: return "Unknown";
    }
}

const char* getClassName(int c) {
    switch(c) {
        case SHIP_CLASS_CONSTITUTION: return "Constitution";
        case SHIP_CLASS_MIRANDA:      return "Miranda";
        case SHIP_CLASS_EXCELSIOR:    return "Excelsior";
        case SHIP_CLASS_CONSTELLATION: return "Constellation";
        case SHIP_CLASS_DEFIANT:      return "Defiant";
        case SHIP_CLASS_GALAXY:       return "Galaxy";
        case SHIP_CLASS_SOVEREIGN:    return "Sovereign";
        case SHIP_CLASS_INTREPID:     return "Intrepid";
        case SHIP_CLASS_AKIRA:        return "Akira";
        case SHIP_CLASS_NEBULA:       return "Nebula";
        case SHIP_CLASS_AMBASSADOR:   return "Ambassador";
        case SHIP_CLASS_OBERTH:       return "Oberth";
        case SHIP_CLASS_STEAMRUNNER:  return "Steamrunner";
        default:                      return "Vessel";
    }
}

void drawHUD(int obj_idx) {
    if (obj_idx < 0 || obj_idx >= objectCount) return;
    
    GameObject *obj = &objects[obj_idx];
    float x = obj->x;
    float y = obj->y;
    float z = obj->z;
    int type = obj->type;
    int id = obj->id;
    int hp = obj->health_pct;

    GLdouble model[16], proj[16];
    GLint view[4];
    GLdouble winX, winY, winZ;

    glGetDoublev(GL_MODELVIEW_MATRIX, model);
    glGetDoublev(GL_PROJECTION_MATRIX, proj);
    glGetIntegerv(GL_VIEWPORT, view);

    /* Project slightly above the ship (1.5 units instead of 0.8) to clear the hull */
    if (gluProject(x, y, z + 1.5f, model, proj, view, &winX, &winY, &winZ) == GL_TRUE) {
        /* Check if behind camera or too close to clip plane */
        if (winZ < 0.0 || winZ > 1.0) return;

        glMatrixMode(GL_PROJECTION);
        glPushMatrix(); glLoadIdentity();
        gluOrtho2D(0, view[2], 0, view[3]);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);

        /* Draw Name/ID - Use a fixed offset from winY to keep it above health bar */
        char buf[128];
        if (type == 1) {
            /* Player: Class (Captain) */
            sprintf(buf, "%s (%s)", getClassName(obj->ship_class), obj->name);
            glColor3f(0.0f, 1.0f, 1.0f); /* Cyan for player */
        } else if (type >= 10 && type < 21) {
            /* NPC: Species [ID] */
            if (type == 12) { /* Borg Special */
                sprintf(buf, "BORG CUBE [%d]", id);
                glColor3f(0.0f, 1.0f, 0.0f); /* Green for Borg */
            } else {
                sprintf(buf, "%s [%d]", (obj->name[0] != '\0') ? obj->name : getSpeciesName(type), id);
                glColor3f(1.0f, 0.3f, 0.3f); /* Redish for NPCs */
            }
        } else {
            /* Other: Name based on type */
            const char* type_name = "Object";
            switch(type) {
                case 3: type_name = "STARBASE"; glColor3f(0.0f, 1.0f, 0.0f); break;
                case 4: {
                    type_name = "STAR"; glColor3f(1.0f, 1.0f, 0.0f);
                    const char* classes[] = {"O (Blue)", "B (Light Blue)", "A (White)", "F (Yellow-White)", "G (Yellow)", "K (Orange)", "M (Red)"};
                    int c_idx = obj->ship_class; if(c_idx<0) c_idx=0; if(c_idx>6) c_idx=6;
                    sprintf(buf, "STAR: %s [%d]", classes[c_idx], id);
                } break;
                case 5: {
                    type_name = "PLANET"; glColor3f(0.0f, 1.0f, 0.5f);
                    const char* res[] = {"-", "Dilithium", "Tritanium", "Verterium", "Monotanium", "Isolinear", "Gases", "Duranium"};
                    int r_idx = obj->ship_class; if(r_idx<0) r_idx=0; if(r_idx>7) r_idx=7;
                    sprintf(buf, "PLANET: %s [%d]", res[r_idx], id);
                } break;
                case 6: type_name = "BLACK HOLE"; glColor3f(0.5f, 0.0f, 1.0f); break;
                case 7: {
                    type_name = "NEBULA"; glColor3f(0.7f, 0.7f, 0.7f);
                    const char* neb_classes[] = {"Mutara Class", "Paulson Class", "Mar Oscura Class", "McAllister Class", "Arachnia Class"};
                    int n_idx = obj->ship_class; if(n_idx<0) n_idx=0; if(n_idx>4) n_idx=4;
                    sprintf(buf, "NEBULA: %s [%d]", neb_classes[n_idx], id);
                } break;
                case 8: type_name = "PULSAR"; glColor3f(1.0f, 0.5f, 0.0f); break;
                case 9: type_name = "COMET"; glColor3f(0.5f, 0.8f, 1.0f); break;
                case 21: type_name = "ASTEROID"; glColor3f(0.6f, 0.4f, 0.2f); break;
                case 22: type_name = "DERELICT"; glColor3f(0.4f, 0.4f, 0.4f); break;
                case 23: type_name = "MINE"; glColor3f(1.0f, 0.0f, 0.0f); break;
                case 24: type_name = "BUOY"; glColor3f(0.0f, 0.5f, 1.0f); break;
                case 25: type_name = "PLATFORM"; glColor3f(1.0f, 0.6f, 0.0f); break;
                case 26: type_name = "RIFT"; glColor3f(0.0f, 1.0f, 1.0f); break;
                case 30: 
                case 31: {
                    type_name = (type == 30) ? "CRYSTALLINE ENTITY" : "SPACE AMOEBA";
                    glColor3f(1.0f, 1.0f, 1.0f);
                    sprintf(buf, "%s [%d]", type_name, id);
                } break;
            }
            if (type == 4 || type == 5) {
                /* Already handled above with specific formatting */
            } else if (obj->name[0] != '\0' && strcmp(obj->name, "Unknown") != 0) {
                sprintf(buf, "%s: %s [%d]", type_name, obj->name, id);
            } else {
                sprintf(buf, "%s [%d]", type_name, id);
            }
        }
        
        glRasterPos2f(winX - (strlen(buf)*4), winY + 25);
        for(int i=0; i<strlen(buf); i++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, buf[i]);

        /* Draw Health Bar (Ships, Bases, Platforms, Monsters) */
        if (type == 1 || type == 3 || (type >= 10 && type <= 20) || type == 22 || type == 25 || type >= 30) {
            float w = 40.0f;
            float h = 4.0f;
            float bar = (hp / 100.0f) * w;
            if (bar < 0) bar = 0; 
            if (bar > w) bar = w;

            /* Border */
            glColor3f(0.5f, 0.5f, 0.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(winX - w/2, winY);
            glVertex2f(winX + w/2, winY);
            glVertex2f(winX + w/2, winY + h);
            glVertex2f(winX - w/2, winY + h);
            glEnd();

            /* Fill */
            if (hp > 50) glColor3f(0.0f, 1.0f, 0.0f);
            else if (hp > 25) glColor3f(1.0f, 1.0f, 0.0f);
            else glColor3f(1.0f, 0.0f, 0.0f);

            glBegin(GL_QUADS);
            glVertex2f(winX - w/2, winY);
            glVertex2f(winX - w/2 + bar, winY);
            glVertex2f(winX - w/2 + bar, winY + h);
            glVertex2f(winX - w/2, winY + h);
            glEnd();

            /* Hull Integrity Text */
            char hbuf[32];
            sprintf(hbuf, "HULL: %d%%", obj->hull_integrity);
            if (obj->hull_integrity > 60) glColor3f(0, 1, 0);
            else if (obj->hull_integrity > 25) glColor3f(1, 1, 0);
            else glColor3f(1, 0, 0);
            glRasterPos2f(winX + w/2 + 5, winY);
            for(int i=0; i<strlen(hbuf); i++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, hbuf[i]);

            /* Plating indicator if present */
            if (obj->plating > 0) {
                char pbuf[32];
                sprintf(pbuf, "HULL: +%d", obj->plating);
                glColor3f(1.0f, 0.8f, 0.0f); /* Yellow for Duranium */
                glRasterPos2f(winX - (strlen(pbuf)*4), winY + 35);
                for(int i=0; i<strlen(pbuf); i++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, pbuf[i]);
            }
        }

        glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING);
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
    }
}

void drawCompass() {
    glDisable(GL_LIGHTING);
    
    /* Global Axes (Standard) */
    glBegin(GL_LINES);
    glColor3f(0.5, 0, 0); glVertex3f(-5.5,0,0); glVertex3f(5.5,0,0); /* X - Redish */
    glColor3f(0, 0.5, 0); glVertex3f(0,-5.5,0); glVertex3f(0,5.5,0); /* Y - Greenish */
    glColor3f(0, 0, 0.5); glVertex3f(0,0,-5.5); glVertex3f(0,0,5.5); /* Z - Bluish */
    glEnd();

    /* Axis Labels */
    glColor3f(1.0, 0.0, 0.0); drawText3D(5.7f, 0, 0, "X");
    glColor3f(0.0, 1.0, 0.0); drawText3D(0, 5.7f, 0, "Y");
    glColor3f(0.3, 0.3, 1.0); drawText3D(0, 0, 5.7f, "Z");

    /* Local Tactical Compass around the ship (origin in shifted space) */
    
    /* 1. Heading Ring (Horizontal plane XZ in OpenGL) - Fixed to World */
    glColor4f(0.0f, 1.0f, 1.0f, 0.3f);
    glBegin(GL_LINE_LOOP);
    for(int i=0; i<360; i+=5) {
        float rad = i * M_PI / 180.0f;
        glVertex3f(sin(rad)*2.5f, 0, cos(rad)*2.5f);
    }
    glEnd();

    /* 2. Heading Labels (every 45 degrees) - Fixed to World */
    glColor3f(0.0f, 0.8f, 0.8f);
    for(int i=0; i<360; i+=45) {
        float rad = i * M_PI / 180.0f;
        float lx = sin(rad)*2.7f;
        float lz = cos(rad)*2.7f;
        char buf[8]; sprintf(buf, "%d", i);
        drawText3D(lx, 0.1f, lz, buf);
    }

    glPushMatrix();
    /* Rotate only the Mark Arc to align with Ship Heading (Augmented Reality) */
    glRotatef(objects[0].h, 0, 1, 0);

    /* 3. Mark Arc (Frontal Vertical plane -90 to +90) */
    glColor4f(1.0f, 1.0f, 0.0f, 0.2f);
    glBegin(GL_LINE_STRIP);
    for(int i=-90; i<=90; i+=5) {
        float rad = i * M_PI / 180.0f;
        /* Using current enterprise heading to align the mark circle */
        float vy = sin(rad) * 2.5f;
        float vz = cos(rad) * 2.5f;
        glVertex3f(0, vy, vz);
    }
    glEnd();

    /* 4. Mark Labels (Only valid tactical range) */
    glColor3f(0.8f, 0.8f, 0.0f);
    int marks[] = {-90, -45, 0, 45, 90};
    for(int i=0; i<5; i++) {
        float rad = marks[i] * M_PI / 180.0f;
        float ly = sin(rad) * 2.8f;
        float lz = cos(rad) * 2.8f;
        char buf[16]; sprintf(buf, "M:%+d", marks[i]);
        drawText3D(0, ly, lz, buf);
    }

    glPopMatrix();
    glEnable(GL_LIGHTING);
}

void drawGlow(float radius, float r, float g, float b, float alpha) {
    glDisable(GL_LIGHTING);
    for (int i = 1; i <= 5; i++) {
        float s = radius * (1.0f + i * 0.2f);
        glColor4f(r, g, b, alpha / (i * 1.5f));
        glutSolidSphere(s, 16, 16);
    }
    glEnable(GL_LIGHTING);
}

void glutSolidSphere_wrapper_saucer() { glutSolidSphere(0.5, 64, 64); }
void glutSolidSphere_wrapper_hull() { glutSolidSphere(0.15, 48, 48); }
void glutSolidSphere_wrapper_defiant() { glutSolidSphere(0.35, 48, 48); }
void glutSolidCube_wrapper() { glutSolidCube(0.5); }

void drawHullDetail(void (*drawFunc)(void), float r, float g, float b) {
    glShadeModel(GL_SMOOTH);
    glColor3f(r, g, b); 
    drawFunc();
    /* Wireframe overlay removed to ensure homogeneous surface */
}

void drawNacelle(float len, float width, float r, float g, float b) {
    /* Nacelle Body (Shaded - High tessellation for smooth look) */
    glPushMatrix(); 
    glScalef(len, width, width); 
    glColor3f(0.45f, 0.45f, 0.5f); 
    glutSolidSphere(0.1, 48, 32); 
    glPopMatrix();

    /* Blue Warp Field Grille (Emissive) */
    GLfloat nac_emit[] = {r*0.7f, g*0.7f, b*0.7f, 1.0f};
    GLfloat no_emit[] = {0.0f, 0.0f, 0.0f, 1.0f};
    glPushMatrix(); 
    glTranslatef(-0.05f * len, 0, 0); 
    glScalef(len*0.6f, width*1.1f, width*1.1f);
    glMaterialfv(GL_FRONT, GL_EMISSION, nac_emit);
    glColor3f(r, g, b);
    glutSolidSphere(0.08, 12, 12);
    glMaterialfv(GL_FRONT, GL_EMISSION, no_emit);
    glPopMatrix();

    /* Bussard Collector (Front Red Tip) - UNLIT for maximum brightness */
    glDisable(GL_LIGHTING);
    glPushMatrix(); 
    glTranslatef(0.1f * len, 0, 0); 
    glColor3f(1.0f, 0.0f, 0.0f); /* Solid Bright Red */
    glutSolidSphere(0.05, 16, 16); 
    /* Extra Glow Layer */
    glEnable(GL_BLEND);
    glColor4f(1.0f, 0.2f, 0.0f, 0.4f);
    glutSolidSphere(0.07, 12, 12);
    glDisable(GL_BLEND);
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

void drawDeflector(float r, float g, float b) {
    glDisable(GL_LIGHTING); glColor3f(r, g, b); glutSolidSphere(0.12, 16, 16); glEnable(GL_LIGHTING);
}

void drawStarfleetSaucer(float sx, float sy, float sz) {
    glPushMatrix(); glScalef(sx, sy, sz); drawHullDetail(glutSolidSphere_wrapper_saucer, 0.88f, 0.88f, 0.92f); glPopMatrix();
    
    /* Luci di posizione superiori */
    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 0.0f, 0.0f); /* Rosso (Port) */
    glPushMatrix(); glTranslatef(0, 0.12f, 0.2f); glutSolidSphere(0.02, 8, 8); glPopMatrix();
    glColor3f(0.0f, 1.0f, 0.0f); /* Verde (Starboard) */
    glPushMatrix(); glTranslatef(0, 0.12f, -0.2f); glutSolidSphere(0.02, 8, 8); glPopMatrix();
    glEnable(GL_LIGHTING);
}

void drawConstitution() {
    glShadeModel(GL_SMOOTH);
    glEnable(GL_LIGHTING);
    drawStarfleetSaucer(1.0f, 0.15f, 1.0f);
    glDisable(GL_LIGHTING); glColor3f(0.0f, 0.5f, 1.0f); glPushMatrix(); glTranslatef(0, 0.1f, 0); glutSolidSphere(0.08, 12, 12); drawGlow(0.06, 0, 0.5, 1, 0.4); glPopMatrix(); glEnable(GL_LIGHTING);
    glPushMatrix(); glTranslatef(-0.2f, -0.1f, 0); glScalef(0.4f, 0.3f, 0.1f); glColor3f(0.8f, 0.8f, 0.85f); glutSolidCube(0.5); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.45f, -0.25f, 0); glScalef(1.8f, 0.8f, 0.8f); drawHullDetail(glutSolidSphere_wrapper_hull, 0.8f, 0.8f, 0.85f); glPopMatrix();
    glPushMatrix(); 
    glTranslatef(-0.15f, -0.25f, 0); 
    glScalef(0.5f, 0.5f, 0.5f); /* Halved size */
    drawDeflector(1.0f, 0.4f, 0.0f); 
    drawGlow(0.1f, 1.0f, 0.3f, 0.0f, 0.5f); 
    glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.5f, -0.1f, side * 0.15f); glRotatef(side*30, 1, 0, 0); glScalef(0.05f, 0.4f, 0.05f); glutSolidCube(1.0); glPopMatrix();
        glPushMatrix(); glTranslatef(-0.6f, 0.15f, side * 0.38f); drawNacelle(4.8, 0.28, 0.2, 0.5, 1.0); glPopMatrix();
    }
}

void drawMiranda() { 
    drawStarfleetSaucer(1.2f, 0.18f, 1.1f);
    glPushMatrix(); glTranslatef(-0.25f, 0.2f, 0); glScalef(0.3f, 0.5f, 0.9f); glColor3f(0.75f, 0.75f, 0.8f); glutSolidCube(0.5); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.25f, 0.1f, side * 0.3f); glScalef(0.1f, 0.4f, 0.1f); glutSolidCube(1.0); glPopMatrix();
        glPushMatrix(); glTranslatef(-0.35f, -0.15f, side * 0.45f); drawNacelle(3.8, 0.4, 0.3, 0.4, 0.8); glPopMatrix();
    }
}

void drawExcelsior() { 
    drawStarfleetSaucer(1.4f, 0.12f, 1.3f);
    glPushMatrix(); glTranslatef(-0.35f, -0.15f, 0); glScalef(0.7f, 0.2f, 0.1f); glutSolidCube(0.5); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.7f, -0.3f, 0); glScalef(2.8f, 0.7f, 0.7f); drawHullDetail(glutSolidSphere_wrapper_hull, 0.8f, 0.8f, 0.85f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.3f, -0.3f, 0); drawDeflector(0.0f, 0.6f, 1.0f); drawGlow(0.1f, 0.2f, 0.7f, 1.0f, 0.4f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.8f, -0.15f, side * 0.35f); drawNacelle(5.5, 0.25, 0.2, 0.6, 1.0); glPopMatrix();
    }
}

void drawConstellation() {
    glPushMatrix(); glScalef(1.2f, 0.4f, 0.9f); drawHullDetail(glutSolidCube_wrapper, 0.75f, 0.75f, 0.8f); glPopMatrix();
    for(int updown=-1; updown<=1; updown+=2) {
        for(int side=-1; side<=1; side+=2) {
            glPushMatrix(); glTranslatef(-0.2f, updown * 0.25f, side * 0.35f); drawNacelle(3.5, 0.3, 0.5, 0.5, 0.6); glPopMatrix();
        }
    }
}

void drawDefiant() {
    glPushMatrix(); glTranslatef(0.3f, 0, 0); glScalef(0.5f, 0.4f, 0.4f); drawDeflector(1.0f, 0.3f, 0.0f); drawGlow(0.15f, 1.0f, 0.2f, 0.0f, 0.4f); glPopMatrix();
    glPushMatrix(); glScalef(1.5f, 0.5f, 1.8f); drawHullDetail(glutSolidSphere_wrapper_defiant, 0.6f, 0.6f, 0.65f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.1f, -0.05f, side * 0.4f); drawNacelle(2.5, 0.6, 0.2, 0.3, 0.7); glPopMatrix();
    }
}

void drawGalaxy() {
    /* 1. Saucer Section (Disco) */
    glPushMatrix();
    drawStarfleetSaucer(1.6f, 0.15f, 2.4f);
    
    /* Bridge Module */
    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 1.0f, 1.0f);
    glPushMatrix(); glTranslatef(0, 0.15f, 0); glScalef(0.3f, 0.1f, 0.2f); glutSolidSphere(0.5, 12, 12); glPopMatrix();
    
    /* Window Lights (Random emitters on rim) */
    glColor3f(1.0f, 1.0f, 0.8f);
    for(int i=0; i<360; i+=30) {
        glPushMatrix();
        glRotatef(i, 0, 1, 0);
        glTranslatef(1.5f, 0, 0);
        glutSolidSphere(0.015, 4, 4);
        glPopMatrix();
    }

    /* Subspace Sensor Probes (Unique to Galaxy Class Command Ship) */
    for(int i=0; i<3; i++) {
        glPushMatrix();
        glRotatef(pulse * 30.0f + (i * 120), 0, 1, 0); 
        glRotatef(30.0f, 1, 0, 1);
        glTranslatef(2.2f, 0, 0);
        glColor3f(0.0f, 0.7f, 1.0f); 
        glutSolidSphere(0.03, 8, 8);
        glPopMatrix();
    }
    glEnable(GL_LIGHTING);
    glPopMatrix();

    /* 2. Neck (Collo) */
    glPushMatrix();
    glTranslatef(-0.4f, -0.15f, 0);
    glColor3f(0.8f, 0.8f, 0.85f);
    glScalef(0.8f, 0.4f, 0.3f);
    glutSolidCube(1.0);
    glPopMatrix();

    /* 3. Secondary Hull (Scafo Secondario) */
    glPushMatrix();
    glTranslatef(-1.0f, -0.3f, 0);
    glScalef(2.2f, 0.8f, 0.9f);
    drawHullDetail(glutSolidSphere_wrapper_hull, 0.85f, 0.85f, 0.9f);
    glPopMatrix();

    /* 4. Advanced Deflector Dish (Corrected Orientation) */
    glPushMatrix();
    glTranslatef(-0.25f, -0.35f, 0);
    glRotatef(90, 0, 1, 0);
    glRotatef(90, 0, 0, 1); /* Makes the 'top' face frontal */
    
    /* Housing */
    glColor3f(0.6f, 0.4f, 0.2f);
    glutSolidTorus(0.05, 0.25, 8, 24);
    
    /* The Dish */
    glPushMatrix();
    glScalef(0.1f, 1.0f, 1.0f);
    glColor3f(0.0f, 0.2f, 0.5f);
    glutSolidSphere(0.22, 16, 16);
    glPopMatrix();

    /* Glowing Core */
    glDisable(GL_LIGHTING);
    float pulse_glow = 0.8f + sin(pulse*3.0f)*0.2f;
    glColor3f(0.0f, 0.6f * pulse_glow, 1.0f * pulse_glow);
    glutSolidSphere(0.08, 12, 12);
    glEnable(GL_LIGHTING);
    glPopMatrix();

    /* 5. Nacelle Pylons (45-degree angle, adhering to hull) */
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix();
        /* Anchor bottom at Z=0.5 on hull, Top at Z=0.85 on nacelle */
        /* dY = 0.35, dZ = 0.35 -> Exact 45 degree angle */
        glTranslatef(-1.0f, 0.075f, side * 0.675f);
        glRotatef(side * 45, 1, 0, 0); 
        glScalef(0.1f, 0.5f, 0.02f); /* Ultra-thin profile */
        glColor3f(0.8f, 0.8f, 0.82f);
        glutSolidCube(1.0);
        glPopMatrix();

        /* 6. Nacelles (FIXED POSITION) */
        glPushMatrix();
        glTranslatef(-1.0f, 0.25f, side * 0.85f);
        drawNacelle(4.5, 0.35, 0.2, 0.6, 1.0);
        glPopMatrix();
    }
}

void drawSovereign() {
    drawStarfleetSaucer(2.2f, 0.12f, 1.3f);
    glPushMatrix(); glTranslatef(-0.7f, -0.15f, 0); glScalef(2.5f, 0.5f, 0.6f); drawHullDetail(glutSolidSphere_wrapper_hull, 0.9f, 0.9f, 0.95f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.3f, -0.15f, 0); drawDeflector(0.0f, 0.4f, 0.8f); drawGlow(0.1f, 0.2f, 0.6f, 1.0f, 0.4f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-1.0f, 0.05f, side * 0.45f); drawNacelle(6.0, 0.2, 0.2, 0.5, 1.0); glPopMatrix();
    }
}

void drawIntrepid() {
    drawStarfleetSaucer(2.0f, 0.15f, 1.0f);
    glPushMatrix(); glTranslatef(-0.6f, -0.15f, 0); glScalef(1.8f, 0.4f, 0.5f); drawHullDetail(glutSolidSphere_wrapper_hull, 0.85f, 0.85f, 0.95f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.25f, -0.15f, 0); drawDeflector(0.0f, 0.5f, 0.9f); drawGlow(0.1f, 0.3f, 0.7f, 1.0f, 0.4f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.8f, 0.1f, side * 0.4f); glRotatef(side*25, 1, 0, 0);
        drawNacelle(3.5, 0.25, 0.3, 0.6, 1.0); glPopMatrix();
    }
}

void drawAkira() {
    glColor3f(0.6f, 0.6f, 0.7f); glPushMatrix(); glScalef(1.4f, 0.2f, 1.8f); drawHullDetail(glutSolidSphere_wrapper_saucer, 0.6f, 0.6f, 0.7f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.6f, -0.2f, side * 0.7f); drawNacelle(4.5, 0.4, 0.4, 0.4, 0.8); glPopMatrix();
        glPushMatrix(); glTranslatef(-0.2f, -0.1f, side * 0.5f); glRotatef(side*30, 1,0,0); glScalef(0.2f, 0.6f, 0.2f); glutSolidCube(1.0); glPopMatrix();
    }
}

void drawNebulaShip() {
    drawGalaxy();
    glPushMatrix(); glTranslatef(-0.6f, 0.4f, 0); glColor3f(0.7f, 0.7f, 0.75f);
    glPushMatrix(); glScalef(0.8f, 0.15f, 0.8f); glutSolidSphere(0.5, 24, 24); glPopMatrix();
    glPushMatrix(); glTranslatef(0, -0.2f, 0); glScalef(0.1f, 0.4f, 0.4f); glutSolidCube(1.0); glPopMatrix();
    glPopMatrix();
}

void drawAmbassador() {
    drawStarfleetSaucer(1.4f, 0.2f, 1.4f);
    glPushMatrix(); glTranslatef(-0.45f, -0.25f, 0); glScalef(1.6f, 0.8f, 0.8f); drawHullDetail(glutSolidSphere_wrapper_hull, 0.8f, 0.8f, 0.85f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.15f, -0.25f, 0); drawDeflector(0.0f, 0.3f, 0.7f); drawGlow(0.1f, 0.2f, 0.5f, 1.0f, 0.4f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.7f, 0.05f, side * 0.45f); drawNacelle(4.5, 0.35, 0.3, 0.4, 0.9); glPopMatrix();
    }
}

void drawOberth() {
    glColor3f(0.9f, 0.9f, 0.9f); glPushMatrix(); glScalef(1.1f, 0.15f, 0.9f); drawHullDetail(glutSolidSphere_wrapper_saucer, 0.9f, 0.9f, 0.9f); glPopMatrix();
    glPushMatrix(); glTranslatef(0, -0.5f, 0); glScalef(1.2f, 0.3f, 0.6f); glutSolidSphere(0.2, 12, 12); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(0, -0.25f, side * 0.35f); glScalef(0.15f, 0.6f, 0.1f); glutSolidCube(1.0); glPopMatrix();
        glPushMatrix(); glTranslatef(-0.35f, -0.25f, side * 0.4f); drawNacelle(2.5, 0.2, 0.4, 0.4, 0.7); glPopMatrix();
    }
}

void drawSteamrunner() {
    glColor3f(0.6f, 0.6f, 0.65f); glPushMatrix(); glScalef(1.6f, 0.25f, 1.5f); drawHullDetail(glutSolidSphere_wrapper_saucer, 0.6f, 0.6f, 0.65f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.7f, -0.05f, side * 0.55f); drawNacelle(3.5, 0.3, 0.2, 0.3, 0.6); glPopMatrix();
        glPushMatrix(); glTranslatef(-0.8f, 0, 0); glScalef(0.1f, 0.1f, 1.0f); glutSolidCube(1.0); glPopMatrix();
    }
}

void drawFederationShip(int class, float h, float m) {
    glRotatef(h - 90.0f, 0, 1, 0);
    glRotatef(m, 0, 0, 1);
    switch(class) {
        case SHIP_CLASS_CONSTITUTION: drawConstitution(); break;
        case SHIP_CLASS_MIRANDA:      drawMiranda(); break;
        case SHIP_CLASS_EXCELSIOR:    drawExcelsior(); break;
        case SHIP_CLASS_CONSTELLATION: drawConstellation(); break;
        case SHIP_CLASS_DEFIANT:      drawDefiant(); break;
        case SHIP_CLASS_GALAXY:       drawGalaxy(); break;
        case SHIP_CLASS_SOVEREIGN:    drawSovereign(); break;
        case SHIP_CLASS_INTREPID:     drawIntrepid(); break;
        case SHIP_CLASS_AKIRA:        drawAkira(); break;
        case SHIP_CLASS_NEBULA:       drawNebulaShip(); break;
        case SHIP_CLASS_AMBASSADOR:   drawAmbassador(); break;
        case SHIP_CLASS_OBERTH:       drawOberth(); break;
        case SHIP_CLASS_STEAMRUNNER:  drawSteamrunner(); break;
        default:                      drawConstitution(); break;
    }
}

void drawKlingon(float x, float y, float z) {
    glPushMatrix(); glColor3f(0.6f, 0.1f, 0.0f); glScalef(1.0f, 0.3f, 1.5f); glutSolidSphere(0.3, 16, 16); glPopMatrix();
    glPushMatrix(); glTranslatef(0.4f, 0, 0); glScalef(2.0f, 0.2f, 0.2f); glutSolidSphere(0.15, 8, 8); glPopMatrix();
    glColor3f(0.8f, 0.0f, 0.0f); glPushMatrix(); glTranslatef(0.7f, 0, 0); glScalef(1.0f, 0.5f, 1.2f); glutSolidSphere(0.15, 12, 12); glPopMatrix();
}

void drawRomulan(float x, float y, float z) {
    glColor3f(0.0f, 0.5f, 0.0f); glPushMatrix(); glScalef(1.5f, 0.2f, 1.0f); glutSolidSphere(0.4, 16, 16); glPopMatrix();
    glPushMatrix(); glTranslatef(0, 0.25f, 0); glScalef(1.5f, 0.2f, 0.8f); glutSolidSphere(0.35, 16, 16); glPopMatrix();
    glPushMatrix(); glTranslatef(0.4f, 0.1f, 0); glScalef(1.0f, 0.5f, 0.2f); glutSolidCube(0.3); glPopMatrix();
    glColor3f(0.0f, 0.7f, 0.2f); glPushMatrix(); glTranslatef(0.7f, 0.1f, 0); glutSolidCone(0.1, 0.3, 8, 8); glPopMatrix();
}

void drawBorg(float x, float y, float z) {
    glRotatef(pulse*5, 1, 1, 1); glColor3f(0.15f, 0.15f, 0.15f); glutWireCube(0.85);
    glColor3f(0.05f, 0.05f, 0.05f); glutSolidCube(0.75); glDisable(GL_LIGHTING);
    float p = (sin(pulse)+1.0f)*0.5f; glColor4f(0.0f, 0.8f * p, 0.0f, 0.6f); glutWireCube(0.8);
    for(int i=0; i<6; i++) {
        glPushMatrix(); if(i==0) glTranslatef(0.38,0,0); else if(i==1) glTranslatef(-0.38,0,0); else if(i==2) glTranslatef(0,0.38,0); else if(i==3) glTranslatef(0,-0.38,0); else if(i==4) glTranslatef(0,0,0.38); else if(i==5) glTranslatef(0,0,-0.38);
        glColor3f(0, 1, 0); glutSolidSphere(0.04, 8, 8); drawGlow(0.03, 0, 1, 0, 0.4); glPopMatrix();
    }
    glEnable(GL_LIGHTING);
}

void drawCardassian(float x, float y, float z) {
    glColor3f(0.6f, 0.5f, 0.3f); glPushMatrix(); glScalef(2.0f, 0.2f, 1.2f); glutSolidSphere(0.4, 16, 16); glPopMatrix();
    glColor3f(0.8f, 0.7f, 0.2f); glPushMatrix(); glTranslatef(0.5f, 0, 0); glScalef(1.0f, 0.4f, 0.4f); glutSolidSphere(0.2, 12, 12); glPopMatrix();
}

void drawJemHadar(float x, float y, float z) {
    glColor3f(0.4f, 0.4f, 0.6f); glPushMatrix(); glScalef(1.2f, 0.5f, 1.0f); glutSolidSphere(0.35, 12, 12); glPopMatrix();
    glPushMatrix(); glTranslatef(0.4f, 0, 0.15f); glutSolidCone(0.05, 0.3, 8, 8); glPopMatrix();
    glPushMatrix(); glTranslatef(0.4f, 0, -0.15f); glutSolidCone(0.05, 0.3, 8, 8); glPopMatrix();
}

void drawTholian(float x, float y, float z) {
    glRotatef(pulse*15, 0, 1, 0); glColor4f(1.0f, 0.5f, 0.0f, 0.6f); glDisable(GL_LIGHTING); glutWireOctahedron();
    glColor4f(1.0f, 0.2f, 0.0f, 0.4f); glutSolidOctahedron(); glEnable(GL_LIGHTING);
}

void drawGorn(float x, float y, float z) {
    glColor3f(0.3f, 0.4f, 0.1f); glPushMatrix(); glScalef(1.5f, 0.6f, 0.6f); glutSolidCube(0.4); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.3f, 0, 0); glScalef(0.5f, 1.2f, 1.5f); glutSolidCube(0.3); glPopMatrix();
}

void drawFerengi(float x, float y, float z) {
    glColor3f(0.7f, 0.3f, 0.1f); glPushMatrix(); glScalef(1.0f, 0.2f, 2.0f); glutSolidSphere(0.4, 16, 16); glPopMatrix();
    glPushMatrix(); glTranslatef(0.3f, 0, 0); glScalef(1.2f, 0.4f, 0.6f); glutSolidSphere(0.3, 12, 12); glPopMatrix();
}

void drawSpecies8472(float x, float y, float z) {
    glRotatef(pulse*10, 1, 0, 1); glColor3f(0.8f, 0.8f, 0.2f); for(int i=0; i<3; i++) { glPushMatrix(); glRotatef(i*120, 0, 1, 0); glTranslatef(0.3f, 0, 0); glScalef(2.0f, 0.3f, 0.3f); glutSolidSphere(0.15, 12, 12); glPopMatrix(); }
    glutSolidSphere(0.2, 12, 12);
}

void drawBreen(float x, float y, float z) {
    glColor3f(0.4f, 0.5f, 0.4f); glPushMatrix(); glScalef(1.8f, 0.2f, 0.8f); glutSolidCube(0.4); glPopMatrix();
    glPushMatrix(); glTranslatef(0.2f, 0.1f, 0.2f); glScalef(0.5f, 0.5f, 1.2f); glutSolidSphere(0.2, 8, 8); glPopMatrix();
}

void drawHirogen(float x, float y, float z) {
    glColor3f(0.5f, 0.5f, 0.5f); glPushMatrix(); glScalef(2.5f, 0.15f, 0.4f); glutSolidSphere(0.35, 12, 12); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.4f, 0, 0); glScalef(0.5f, 0.8f, 1.5f); glutSolidCube(0.2); glPopMatrix();
}

void drawStarbase(float x, float y, float z) {
    glRotatef(pulse*10, 0, 1, 0); glColor3f(0.9f, 0.9f, 0.1f); glutWireSphere(0.4, 12, 12);
    glColor3f(0.5f, 0.5f, 0.5f); glPushMatrix(); glScalef(1.5f, 0.1f, 1.5f); glutSolidCube(0.6); glPopMatrix();
}

void drawStar(float x, float y, float z, int id) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);

    /* Spectral Class Color based on ID */
    float r = 1.0f, g = 1.0f, b = 1.0f;
    int type = id % 7;
    if (type == 0) { r=0.4f; g=0.6f; b=1.0f; }      /* Class O - Blue */
    else if (type == 1) { r=0.7f; g=0.8f; b=1.0f; } /* Class B - Light Blue */
    else if (type == 2) { r=1.0f; g=1.0f; b=1.0f; } /* Class A - White */
    else if (type == 3) { r=1.0f; g=1.0f; b=0.7f; } /* Class F - Yellow-White */
    else if (type == 4) { r=1.0f; g=0.9f; b=0.1f; } /* Class G - Yellow (Sol) */
    else if (type == 5) { r=1.0f; g=0.6f; b=0.2f; } /* Class K - Orange */
    else { r=1.0f; g=0.2f; b=0.1f; }                /* Class M - Red */

    /* 3. Core */
    float core_size = 0.38f;
    
    /* Violent pulsation if this is the supernova star */
    /* Check using the world coordinates passed to the function AND global quadrant */
    if (g_sn_pos.active && g_sn_q[0] == g_my_q[0] && g_sn_q[1] == g_my_q[1] && g_sn_q[2] == g_my_q[2] &&
        fabs(x - g_sn_pos.x) < 0.1f && fabs(y - g_sn_pos.y) < 0.1f && fabs(z - g_sn_pos.z) < 0.1f) {
        float sn_factor = 1.0f - (g_sn_pos.timer / 1800.0f); /* 0 to 1 as it nears explosion */
        core_size += sn_factor * 0.4f * (sin(pulse*30.0f)*0.5f + 0.5f);
        glColor3f(1.0f, 1.0f, 1.0f); /* Turning white hot */
    } else {
        glColor3f(r, g, b);
    }
    glutSolidSphere(core_size, 32, 32);

    /* 2. ATMOSPHERIC LAYERS (Corona): Semi-transparent and glowing */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); /* Additive glow */
    glDepthMask(GL_FALSE); /* Aura doesn't block depth */

    for(int i=0; i<3; i++) {
        glPushMatrix();
        glRotatef(pulse * (10.0f + i*5.0f), 0, 1, 0);
        glRotatef(pulse * (5.0f + i*2.0f), 1, 0, 0);
        float s = 0.4f + i*0.12f + sin(pulse*3.0f + i)*0.03f;
        glColor4f(r, g, b, 0.3f / (i+1));
        glutSolidSphere(s, 16, 16);
        glPopMatrix();
    }
    
    /* 3. Solar Flares (Prominences) */
    glLineWidth(2.0f);
    for(int i=0; i<8; i++) {
        glPushMatrix();
        glRotatef(i*45 + pulse*20, 0, 1, 0);
        glRotatef(sin(pulse + i)*30, 0, 0, 1);
        float flare_len = 0.45f + sin(pulse*5.0f + i)*0.2f;
        glBegin(GL_LINES);
        glColor4f(r, g, b, 0.7f);
        glVertex3f(0.3f, 0, 0);
        glColor4f(r, g, b, 0.0f);
        glVertex3f(flare_len, 0, 0);
        glEnd();
        glPopMatrix();
    }

    glPopAttrib();
}

void drawPlanet(float x, float y, float z) {
    GLfloat spec[] = {0.3f, 0.3f, 0.3f, 1.0f};
    glMaterialfv(GL_FRONT, GL_SPECULAR, spec);
    glMateriali(GL_FRONT, GL_SHININESS, 50);
    
    glColor3f(0.2f, 0.6f, 1.0f);
    glutSolidSphere(0.3, 24, 24);
    
    /* Atmosphere */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 1.0f, 1.0f, 0.2f);
    glutSolidSphere(0.32, 24, 24);
    glDisable(GL_BLEND);
}

void drawWormhole(float x, float y, float z, float h, float m, int type) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(h - 90.0f, 0, 1, 0);
    glRotatef(m, 0, 0, 1);
    glRotatef(90, 0, 1, 0); /* Align longitudinal axis with ship forward */

    /* 1. Draw the central 3D sphere with material properties */
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    
    if (type == 0) {
        /* DEPARTURE: Dark charcoal with metallic specularity */
        float mat_amb[] = {0.0f, 0.0f, 0.0f, 1.0f};
        float mat_diff[] = {0.02f, 0.02f, 0.02f, 1.0f};
        float mat_spec[] = {0.6f, 0.6f, 0.6f, 1.0f};
        glMaterialfv(GL_FRONT, GL_AMBIENT, mat_amb);
        glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_diff);
        glMaterialfv(GL_FRONT, GL_SPECULAR, mat_spec);
        glMaterialf(GL_FRONT, GL_SHININESS, 100.0f);
        glColor3f(0.05f, 0.05f, 0.05f);
    } else {
        /* ARRIVAL: Golden White Star */
        float mat_amb[] = {0.5f, 0.4f, 0.2f, 1.0f};
        float mat_diff[] = {1.0f, 0.9f, 0.7f, 1.0f};
        float mat_spec[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glMaterialfv(GL_FRONT, GL_AMBIENT, mat_amb);
        glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_diff);
        glMaterialfv(GL_FRONT, GL_SPECULAR, mat_spec);
        glMaterialf(GL_FRONT, GL_SHININESS, 128.0f);
        glColor3f(1.0f, 1.0f, 1.0f);
    }
    
    /* Using glutSolidSphere instead of GLUquadric to avoid memory issues */
    glutSolidSphere(0.35f, 32, 32);
    glDisable(GL_LIGHTING);

    /* 2. Activate shader for the external energy cones */
    if (whShaderProgram) {
        glUseProgram(whShaderProgram);
        glUniform1f(glGetUniformLocation(whShaderProgram, "time"), pulse);
    }

    glRotatef(pulse * 25.0f, 0, 0, 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); 
    glDisable(GL_DEPTH_TEST); 
    
    float rs = 0.45f;
    float rmax = 1.6f;
    int Nr = 15;
    int Nt = 30;
    float dr = (rmax - rs) / (float)Nr;
    float dth = 2.0f * M_PI / (float)Nt;

    for (int side = -1; side <= 1; side += 2) {
        for(int i=0; i<Nr; i++){
            float r = rs + (float)i * dr;
            float zz = 0.6f + 2.0f * sqrt(rs * (r - rs));
            if (type == 1) zz = 2.64f - zz; /* Invert orientation for arrival: Wide at center, tip away */
            float fade = 1.0f - (float)i/(float)Nr;
            if (type == 0) glColor4f(0.3f*fade, 0.0f, 1.0f*fade, 0.8f*fade); /* Blue/Purple Cones */
            else glColor4f(1.0f*fade, 0.8f*fade, 0.2f*fade, 0.8f*fade);      /* Gold Cones */
            glBegin(GL_LINE_LOOP);
            for(int j=0; j<=Nt; j++){
                float th = (float)j * dth;
                glVertex3f(r * cos(th), r * sin(th), (float)side * zz);
            }
            glEnd();
        }
        for(int j=0; j<Nt; j++){
            float th = (float)j * dth;
            glBegin(GL_LINE_STRIP);
            for(int i=0; i<Nr; i++){
                float r = rs + (float)i * dr;
                float zz = 0.6f + 2.0f * sqrt(rs * (r - rs));
                if (type == 1) zz = 2.64f - zz; /* Match inverted orientation */
                float fade = 1.0f - (float)i/(float)Nr;
                if (type == 0) glColor4f(0.2f*fade, 0.0f, 0.6f*fade, 0.6f*fade);
                else glColor4f(0.8f*fade, 0.6f*fade, 0.2f*fade, 0.6f*fade);
                glVertex3f(r * cos(th), r * sin(th), (float)side * zz);
            }
            glEnd();
        }
    }
    glPopAttrib();
    glPopMatrix();
    glUseProgram(0);
}

void drawBlackHole(float x, float y, float z) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);
    glTranslatef(x, y, z);
    
    /* 1. RELATIVISTIC JETS - Rendered with additive blending */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    for(int i=-1; i<=1; i+=2) {
        if(i==0) continue;
        glPushMatrix();
        float jet_pulse = (sin(pulse*10.0f)+1.0f)*0.5f;
        glColor4f(0.5f, 0.4f, 1.0f, 0.3f + jet_pulse*0.2f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(0, 3.5f * i, 0); 
        for(int j=0; j<=16; j++) {
            float ang = j * (2.0f * M_PI / 16.0f);
            float r = 0.08f + jet_pulse*0.04f;
            glColor4f(0.2f, 0.0f, 0.6f, 0.0f);
            glVertex3f(r*cos(ang), 0.1f * i, r*sin(ang));
        }
        glEnd();
        glPopMatrix();
    }

    /* 2. SOLID EVENT HORIZON - MUST BE OPAQUE BLACK */
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glColor3f(0.0f, 0.0f, 0.0f);
    glutSolidSphere(0.2, 32, 32); 

    /* 3. ACCRETION DISK - Volumetric and tilted */
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glPushMatrix();
    glRotatef(75, 1, 0, 0); 
    glRotatef(pulse*50, 0, 0, 1);
    for(int i=0; i<10; i++) {
        float r = 0.24f + i*0.06f;
        float alpha = 0.7f - i*0.07f;
        glColor4f(0.8f - i*0.05f, 0.2f + i*0.05f, 1.0f, alpha);
        glutWireTorus(0.01, r, 10, 60);
        
        if(i % 2 == 0) {
            glColor4f(0.4f, 0.1f, 0.7f, 0.1f);
            glutSolidTorus(0.02, r, 8, 40);
        }
    }
    glPopMatrix();
    
    /* 4. PHOTON RING - Bright edge */
    glColor4f(1.0f, 0.8f, 1.0f, 0.8f);
    glutWireSphere(0.21, 32, 32);

    /* 5. GRAVITATIONAL GLOW */
    glRotatef(pulse*5, 0, 1, 1);
    glColor4f(0.3f, 0.0f, 0.6f, 0.15f);
    glutSolidSphere(0.8, 20, 20);
    
    glPopAttrib();
}

void drawStellarNebula(float x, float y, float z) {
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    /* Multilayered gas cloud */
    for (int i = 0; i < 5; i++) {
        glPushMatrix();
        glRotatef(pulse * (3.0f + i * 1.5f), 0, 1, 0);
        glRotatef(pulse * (2.0f + i), 1, 0, 0);
        
        float scale = 1.0f + i * 0.4f;
        float alpha = 0.25f - (i * 0.04f);
        if (alpha < 0.05f) alpha = 0.05f;
        
        /* Shift between Purple and Blue */
        glColor4f(0.4f + i*0.1f, 0.2f, 0.6f + i*0.05f, alpha);
        
        glScalef(scale, scale * 0.8f, scale * 1.2f);
        glutSolidSphere(1.0f, 16, 16);
        glPopMatrix();
    }
    
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

void drawPulsar(float x, float y, float z) {
    glDisable(GL_LIGHTING);
    /* Core */
    glColor3f(1.0f, 1.0f, 1.0f);
    glutSolidSphere(0.2, 16, 16);
    
    /* Beams */
    glPushMatrix();
    glRotatef(pulse * 100.0f, 0, 1, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_LINES);
    for (int i = -1; i <= 1; i+=2) {
        glColor4f(1.0f, 0.5f, 0.0f, 0.8f);
        glVertex3f(0, 0, 0);
        glColor4f(1.0f, 0.2f, 0.0f, 0.0f);
        glVertex3f(0, 4.0f * i, 0);
    }
    glEnd();
    glDisable(GL_BLEND);
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

void drawComet(float x, float y, float z) {
    glDisable(GL_LIGHTING);
    glPushMatrix();
    
    /* Irregular Nucleus */
    glPushMatrix();
    glRotatef(pulse*5, 1, 0, 1);
    glColor3f(0.8f, 0.8f, 1.0f);
    glScalef(1.2f, 0.8f, 0.9f);
    glutSolidSphere(0.12f, 10, 10);
    glPopMatrix();
    
    /* Coma (Glow) */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.4f, 0.6f, 1.0f, 0.5f);
    glutSolidSphere(0.25f, 16, 16);
    
    /* Volumetric Tail (Multiple cones/layers) */
    /* Point tail in negative X direction (opposite to movement) */
    glPushMatrix();
    glRotatef(90, 0, 1, 0); 
    for(int i=0; i<3; i++) {
        glPushMatrix();
        float length = 1.5f + i*0.5f;
        float width = 0.2f + i*0.1f;
        float alpha = 0.4f - i*0.1f;
        glColor4f(0.5f, 0.7f, 1.0f, alpha);
        glRotatef(sin(pulse*2 + i)*5, 1, 0, 0);
        glutSolidCone(width, length, 12, 4);
        glPopMatrix();
    }
    glPopMatrix();
    
    glDisable(GL_BLEND);
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

void drawAsteroid(float x, float y, float z) {
    glPushMatrix();
    glColor3f(0.5f, 0.35f, 0.25f); /* Brownish */
    glRotatef(pulse * 10.0f, 1, 1, 0);
    
    /* Main rocky body (Irregular composition) */
    glPushMatrix();
    glScalef(1.2f, 0.9f, 1.1f);
    glutSolidCube(0.2);
    glPopMatrix();
    
    /* Random bumps for a more 3D asteroid look */
    for(int i=0; i<4; i++) {
        glPushMatrix();
        glRotatef(i*90, 0, 1, 1);
        glTranslatef(0.1f, 0, 0);
        glScalef(0.6f, 0.5f, 0.7f);
        glutSolidCube(0.15);
        glPopMatrix();
    }
    glPopMatrix();
}

void drawDerelict(int ship_class) {
    glPushMatrix();
    /* Slow drifting rotation */
    glRotatef(pulse * 2.0f, 0.3, 1.0, 0.2);
    /* Dark hull, no emissive lights */
    glColor3f(0.3f, 0.3f, 0.32f);
    drawFederationShip(ship_class, 0, 0);
    glPopMatrix();
}

void drawMine(float x, float y, float z) {
    glPushMatrix();
    glRotatef(pulse * 50.0f, 1, 0, 1);
    
    /* Core */
    glColor3f(0.4f, 0.4f, 0.45f);
    glutSolidSphere(0.1f, 8, 8);
    
    /* Spikes */
    glColor3f(0.3f, 0.3f, 0.3f);
    for(int i=0; i<6; i++) {
        glPushMatrix();
        if(i==0) glRotatef(90, 1,0,0);
        else if(i==1) glRotatef(-90, 1,0,0);
        else if(i==2) glRotatef(90, 0,1,0);
        else if(i==3) glRotatef(-90, 0,1,0);
        else if(i==4) glRotatef(90, 0,0,1);
        else if(i==5) glRotatef(-90, 0,0,1);
        glTranslatef(0, 0, 0.15f);
        glutSolidCone(0.02, 0.1, 4, 4);
        glPopMatrix();
    }
    
    /* Pulsing Light */
    float p = 0.5f + sin(pulse*10.0f)*0.5f;
    glDisable(GL_LIGHTING);
    glColor3f(p, 0, 0);
    glutSolidSphere(0.04f, 8, 8);
    glEnable(GL_LIGHTING);
    
    glPopMatrix();
}

void drawBuoy(float x, float y, float z) {
    glPushMatrix();
    /* Main body */
    glColor3f(0.6f, 0.6f, 0.7f);
    glutSolidCube(0.15);
    
    /* Lattice structure */
    glColor3f(0.4f, 0.4f, 0.5f);
    glBegin(GL_LINES);
    glVertex3f(0, 0, 0); glVertex3f(0, 0.5, 0);
    glVertex3f(0, 0.5, 0); glVertex3f(0.2, 0.7, 0);
    glVertex3f(0, 0.5, 0); glVertex3f(-0.2, 0.7, 0);
    glEnd();
    
    /* Rotating Antenna */
    glPushMatrix();
    glTranslatef(0, 0.5, 0);
    glRotatef(pulse * 40.0f, 0, 1, 0);
    glColor3f(0.8f, 0.8f, 0.0f);
    glBegin(GL_TRIANGLES);
    glVertex3f(0, 0, 0); glVertex3f(0.1, 0.2, 0.05); glVertex3f(0.1, 0.2, -0.05);
    glEnd();
    glPopMatrix();
    
    /* Blue Signal Rings (Pulse) */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    float ring_s = fmod(pulse * 0.5f, 1.0f);
    glColor4f(0.0f, 0.5f, 1.0f, 1.0f - ring_s);
    glPushMatrix();
    glRotatef(90, 1, 0, 0);
    glutWireTorus(0.01, ring_s * 0.8f, 8, 20);
    glPopMatrix();
    glDisable(GL_BLEND);
    
    glPopMatrix();
}

void drawPlatform(float x, float y, float z) {
    glPushMatrix();
    glRotatef(pulse * 5.0f, 0, 1, 0);
    
    /* Main Hexagonal Body */
    glColor3f(0.4f, 0.4f, 0.4f);
    for(int i=0; i<3; i++) {
        glPushMatrix();
        glRotatef(i*120, 0, 1, 0);
        glScalef(1.0f, 0.4f, 0.3f);
        glutSolidCube(1.0);
        glPopMatrix();
    }
    
    /* Phaser Banks (Top/Bottom) */
    glColor3f(0.2f, 0.2f, 0.2f);
    glPushMatrix(); glTranslatef(0, 0.25, 0); glutSolidCylinder(0.1, 0.1, 12, 2); glPopMatrix();
    glPushMatrix(); glTranslatef(0, -0.35, 0); glutSolidCylinder(0.1, 0.1, 12, 2); glPopMatrix();
    
    /* Energy Core */
    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 0.2f, 0.0f);
    glutSolidSphere(0.15, 12, 12);
    glEnable(GL_LIGHTING);
    
    glPopMatrix();
}

void drawRift(float x, float y, float z) {
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); /* Additive blending */
    glPushMatrix();
    
    /* Rotating energy rings */
    for (int i = 0; i < 5; i++) {
        glPushMatrix();
        glRotatef(pulse * (20.0f + i * 10.0f), 0.2, 1.0, 0.5);
        float r = 0.3f + i * 0.1f;
        glColor4f(0.0f, 0.8f, 1.0f, 0.6f - i * 0.1f);
        glutWireTorus(0.02, r, 8, 24);
        glPopMatrix();
    }
    
    /* Inner crackle */
    glColor4f(1.0f, 1.0f, 1.0f, 0.8f);
    glutSolidSphere(0.1f + sin(pulse*20.0f)*0.02f, 8, 8);
    
    glPopMatrix();
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

void drawMonster(int type, float x, float y, float z) {
    if (type == 30) { /* Crystalline Entity */
        glDisable(GL_LIGHTING);
        glPushMatrix();
        glRotatef(pulse * 20.0f, 1, 1, 1);
        glColor3f(1.0f, 1.0f, 1.0f);
        glutWireIcosahedron();
        for(int i=0; i<4; i++) {
            glPushMatrix();
            glRotatef(i*90, 0, 1, 0);
            glScalef(0.2f, 2.0f, 0.2f);
            glColor4f(0.8f, 0.5f, 1.0f, 0.8f);
            glutSolidCube(1.0);
            glPopMatrix();
        }
        glPopMatrix();
        glEnable(GL_LIGHTING);
    } else if (type == 31) { /* Space Amoeba */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_LIGHTING);
        glPushMatrix();
        float s = 1.0f + sin(pulse*2.0f)*0.1f;
        glScalef(s, s*0.8f, s*1.1f);
        glColor4f(0.0f, 0.6f, 0.2f, 0.6f);
        glutSolidSphere(1.5, 16, 16);
        /* Nucleus */
        glColor4f(0.8f, 0.2f, 0.0f, 0.8f);
        glutSolidSphere(0.4, 8, 8);
        glPopMatrix();
        glDisable(GL_BLEND);
        glEnable(GL_LIGHTING);
    }
}

void drawGalaxyMap() {
    glDisable(GL_LIGHTING);
    float gap = 1.2f;
    float offset = - (10.0f * gap) / 2.0f;

    /* Draw full grid frame */
    glColor4f(0.2f, 0.2f, 0.5f, 0.3f);
    glPushMatrix();
    glTranslatef(0, 0, 0);
    glScalef(10.0f * gap, 10.0f * gap, 10.0f * gap);
    glutWireCube(1.0);
    glPopMatrix();

    /* Draw Vertex Coordinates for orientation */
    glColor3f(0.5f, 0.5f, 0.5f);
    char vbuf[32];
    int v_coords[] = {1, 10};
    for(int vz=0; vz<2; vz++) {
        for(int vy=0; vy<2; vy++) {
            for(int vx=0; vx<2; vx++) {
                int cx = v_coords[vx], cy = v_coords[vy], cz = v_coords[vz];
                float px = offset + cx * gap;
                float py = offset + cz * gap;
                float pz = offset + (11 - cy) * gap; /* Inverted Y for map display consistency */
                sprintf(vbuf, "[%d,%d,%d]", cx, cy, cz);
                drawText3D(px, py + 0.3f, pz, vbuf);
            }
        }
    }

    for(int z=1; z<=10; z++) {
        for(int y=1; y<=10; y++) {
            for(int x=1; x<=10; x++) {
                int64_t val = g_galaxy[x][y][z];
                bool is_my_q = (x == g_my_q[0] && y == g_my_q[1] && z == g_my_q[2]);
                if (val == 0 && !is_my_q) continue;

                float px = offset + x * gap;
                float py = offset + z * gap;
                float pz = offset + (11 - y) * gap;

                /* Color coding based on M|Su|R|T|B|M|D|A|C|S|Pu|N|BH|P|K|B|S (17 digits) */
                int monster  = (val / 10000000000000000LL) % 10;
                int rift     = (val / 100000000000000LL) % 10;
                int platform = (val / 10000000000000LL) % 10;
                int buoy     = (val / 1000000000000LL) % 10;
                int mine     = (val / 100000000000LL) % 10;
                int derelict = (val / 10000000000LL) % 10;
                int asteroid = (val / 1000000000LL) % 10;
                int comet = (val / 100000000LL) % 10;
                int storm = (val / 10000000LL) % 10;
                int pul   = (val / 1000000LL) % 10;
                int neb   = (val / 100000LL) % 10;
                int bh    = (val / 10000LL) % 10;
                int pl    = (val / 1000LL) % 10;
                int en    = (val / 100LL) % 10;
                int bs    = (val / 10LL) % 10;
                int st    = val % 10;

                glPushMatrix();
                glTranslatef(px, py, pz);
                
                if (is_my_q) {
                    /* Highlight current quadrant: Pulsing White + Label */
                    float s_glow = 0.4f + sin(pulse*6.0f)*0.15f;
                    glColor4f(1, 1, 1, 0.8);
                    glutWireCube(s_glow);
                    glColor3f(1, 1, 1);
                    drawText3D(-0.3f, 0.4f, 0, "YOU");
                }

                if (g_sn_pos.active && x == g_sn_q[0] && y == g_sn_q[1] && z == g_sn_q[2]) {
                    /* Supernova Warning - Red Pulsing (Global Broadcast) */
                    float blink = (sin(pulse * 10.0f) + 1.0f) * 0.5f;
                    glColor4f(1.0f, 0.0f, 0.0f, 0.3f + blink * 0.5f);
                    glutSolidCube(gap * 0.8f);
                }

                if (storm > 0) {
                    /* Ion Storm: Large Transparent White Wireframe Shell - Enhanced visibility */
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    glLineWidth(2.0f);
                    glColor4f(1.0f, 1.0f, 1.0f, 0.4f + sin(pulse*4.0f)*0.2f);
                    glutWireCube(0.8f);
                    glLineWidth(1.0f);
                    glDisable(GL_BLEND);
                }

                if (val > 0) {
                    if (monster > 0) glColor3f(1.0, 1.0, 1.0); /* White - Monster */
                    else if (rift > 0) glColor3f(0.0, 1.0, 1.0); /* Cyan - Rift */
                    else if (platform > 0) glColor3f(0.8, 0.4, 0.0); /* Dark Orange - Platform */
                    else if (buoy > 0) glColor3f(0.0, 0.5, 1.0); /* Blue - Buoy */
                    else if (mine > 0) glColor3f(1.0, 0.0, 0.0); /* Bright Red - Mine */
                    else if (derelict > 0) glColor3f(0.3, 0.3, 0.3); /* Dark Grey - Derelict */
                    else if (asteroid > 0) glColor3f(0.5, 0.3, 0.1); /* Brown - Asteroid */
                    else if (comet > 0) glColor3f(0.5, 0.8, 1.0); /* Light Blue - Comet */
                    else if (pul > 0) glColor3f(1.0, 0.5, 0); /* Orange - Pulsar */
                    else if (neb > 0) glColor3f(0.7, 0.7, 0.7); /* Grey - Nebula */
                    else if (bh > 0) glColor3f(0.6, 0, 1.0); /* Purple - Black Hole */
                    else if (en > 0) glColor3f(1, 0, 0); /* Red - Hostile */
                    else if (bs > 0) glColor3f(0, 1, 0); /* Green - Base */
                    else if (pl > 0) glColor3f(0, 0.8, 1); /* Cyan - Planet */
                    else if (st > 0) glColor3f(1, 1, 0); /* Yellow - Star */
                    else glColor3f(0.4, 0.4, 0.4); /* Dark gray fallback */
                    
                    float base_s = 0.15f;
                    if (pul > 0) base_s += sin(pulse*8.0f)*0.05f; /* Pulsar heartbeat */
                    if (monster > 0) base_s = 0.25f + sin(pulse*5.0f)*0.05f;
                    if (mine > 0) base_s = 0.1f;
                    if (buoy > 0) base_s = 0.12f;
                    if (platform > 0) base_s = 0.18f;
                    if (rift > 0) base_s = 0.2f;
                    glutSolidCube(base_s);
                }
                glPopMatrix();
            }
        }
    }
    glEnable(GL_LIGHTING);
}

void drawGrid() {
    glDisable(GL_LIGHTING); glColor4f(0.5f, 0.5f, 0.5f, 0.2f);
    if (vbo_grid != 0) { glEnableClientState(GL_VERTEX_ARRAY); glBindBuffer(GL_ARRAY_BUFFER, vbo_grid); glVertexPointer(3, GL_FLOAT, 0, 0); glDrawArrays(GL_LINES, 0, grid_vertex_count); glBindBuffer(GL_ARRAY_BUFFER, 0); glDisableClientState(GL_VERTEX_ARRAY); }
    glEnable(GL_LIGHTING);
}

void drawShipTrail(int obj_idx) {
    glDisable(GL_LIGHTING);
    glLineWidth(2.0f);
    glBegin(GL_LINE_STRIP);
    
    float r=0.4, g=0.7, b=1.0; /* Default Starfleet Blue */
    if (objects[obj_idx].type == 10) { r=1.0; g=0.1; b=0.0; } /* Klingon Red */
    if (objects[obj_idx].type == 11) { r=0.0; g=1.0; b=0.2; } /* Romulan Green */
    if (objects[obj_idx].type == 12) { r=0.0; g=0.8; b=0.8; } /* Borg Cyan */

    for (int i = 0; i < objects[obj_idx].trail_count; i++) {
        int idx = (objects[obj_idx].trail_ptr - 1 - i + MAX_TRAIL) % MAX_TRAIL;
        float alpha = (1.0f - (float)i / objects[obj_idx].trail_count) * 0.5f;
        glColor4f(r, g, b, alpha);
        glVertex3f(objects[obj_idx].trail[idx][0], objects[obj_idx].trail[idx][1], objects[obj_idx].trail[idx][2]);
    }
    glEnd();
    glEnable(GL_LIGHTING);
}

void drawPhaserBeams() {
    glDisable(GL_LIGHTING);
    for (int i = 0; i < 10; i++) {
        if (beams[i].alpha > 0) {
            glLineWidth(4.0f); glColor4f(1.0f, 0.8f, 0.0f, (beams[i].alpha > 1.0f) ? 1.0f : beams[i].alpha);
            glBegin(GL_LINES); glVertex3f(enterpriseX, enterpriseY, enterpriseZ); glVertex3f(beams[i].x, beams[i].y, beams[i].z); glEnd();
        }
    }
    glEnable(GL_LIGHTING);
}

void drawExplosion() {
    if (g_boom.timer <= 0) return;
    glDisable(GL_LIGHTING);
    glPushMatrix(); glTranslatef(g_boom.x, g_boom.y, g_boom.z);
    /* Pulsing expansion */
    float s = 0.5f + (40 - g_boom.timer) * 0.05f;
    float alpha = g_boom.timer / 40.0f;
    glColor4f(1.0f, 0.5f, 0.0f, alpha); 
    glutSolidSphere(s, 16, 16);
    glPopMatrix(); glEnable(GL_LIGHTING);
}

void drawJumpArrival() {
    if (g_jump_arrival.timer <= 0) return;
    
    glPushMatrix();
    glTranslatef(g_jump_arrival.x, g_jump_arrival.y, g_jump_arrival.z);
    
    /* Phase 1: Pure Golden Wormhole (Timer 300 -> 120 : 3 seconds @ 60fps) */
    /* Phase 2: Ship materialization with Flash (Timer 120 -> 0) */
    float wh_scale = (g_jump_arrival.timer < 60) ? (g_jump_arrival.timer / 60.0f) : 1.0f;
    
    glPushMatrix();
    glScalef(wh_scale, wh_scale, wh_scale);
    drawWormhole(0, 0, 0, g_jump_arrival.h, g_jump_arrival.m, 1); /* Type 1: Arrival (Gold) */
    glPopMatrix();
    
    /* Delay the flash: only starts when timer < 120 (after 3 seconds @ 60fps) */
    if (g_jump_arrival.timer < 120) {
        float flash_t = 1.0f - (g_jump_arrival.timer / 120.0f);
        drawGlow(0.5f + flash_t * 5.0f, 1.0f, 1.0f, 1.0f, (1.0f - flash_t) * 0.9f);
    }

    glPopMatrix();
}

void drawTorpedo() {
    if (!g_torp.active) return;
    glDisable(GL_LIGHTING);
    glPushMatrix(); glTranslatef(g_torp.x, g_torp.y, g_torp.z);
    
    /* Halved sizes and improved color contrast */
    float wave = (sin(pulse * 20.0f) + 1.0f) * 0.5f;
    
    /* 1. Undulating Aura (Yellow/Orange) - Rendered FIRST to avoid obscuring the red core */
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    for(int i=0; i<2; i++) {
        glPushMatrix();
        float s = 0.03f + (i * 0.02f) * wave;
        float alpha = 0.4f / (i + 1);
        glColor4f(1.0f, 0.6f, 0.0f, alpha);
        glScalef(1.0f + wave*0.3f, 1.0f - wave*0.2f, 1.0f + wave*0.2f);
        glutWireSphere(s, 8, 8);
        glPopMatrix();
    }

    /* 2. Micro Central Glow (Orange/Yellow tint) */
    drawGlow(0.05f + wave*0.02f, 1.0f, 0.4f, 0.0f, 0.3f);
    glDisable(GL_BLEND);

    /* 3. Small Intense Red Core - Rendered LAST to be on top */
    glColor3f(1.0f, 0.0f, 0.0f);
    glutSolidSphere(0.02, 10, 10);
    
    glPopMatrix(); glEnable(GL_LIGHTING);
}

void drawDismantle() {
    if (g_dismantle.timer <= 0) return;
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDisable(GL_LIGHTING);
    
    float alpha = g_dismantle.timer / 60.0f;
    
    for(int i=0; i<100; i++) {
        if (g_dismantle.particles[i].active) {
            glPushMatrix();
            glTranslatef(g_dismantle.particles[i].x, g_dismantle.particles[i].y, g_dismantle.particles[i].z);
            glColor4f(g_dismantle.particles[i].r, g_dismantle.particles[i].g, g_dismantle.particles[i].b, alpha);
            glutSolidSphere(0.05f * alpha, 8, 8);
            glPopMatrix();
        }
    }
    
    glEnable(GL_LIGHTING);
    glDisable(GL_BLEND);
}

void drawFaceLabels() {
    if (!g_show_hud || g_show_map) return;
    
    int q1 = g_my_q[0];
    int q2 = g_my_q[1];
    int q3 = g_my_q[2];

    struct {
        float x, y, z;
        int nq[3];
    } faces[6] = {
        { 5.5f, 0, 0, {q1+1, q2, q3} },   /* Right (X+) */
        {-5.5f, 0, 0, {q1-1, q2, q3} },   /* Left (X-) */
        { 0, 5.5f, 0, {q1, q2, q3+1} },   /* Top (Z+) -> Y+ in viewer */
        { 0,-5.5f, 0, {q1, q2, q3-1} },   /* Down (Z-) -> Y- in viewer */
        { 0, 0,-5.5f, {q1, q2+1, q3} },   /* Fore (Y+) -> Z- in viewer */
        { 0, 0, 5.5f, {q1, q2-1, q3} }    /* Aft (Y-) -> Z+ in viewer */
    };

    GLdouble model[16], proj[16];
    GLint view[4];
    glGetDoublev(GL_MODELVIEW_MATRIX, model);
    glGetDoublev(GL_PROJECTION_MATRIX, proj);
    glGetIntegerv(GL_VIEWPORT, view);

    for(int i=0; i<6; i++) {
        if (!IS_Q_VALID(faces[i].nq[0], faces[i].nq[1], faces[i].nq[2])) continue;
        
        GLdouble winX, winY, winZ;
        if (gluProject(faces[i].x, faces[i].y, faces[i].z, model, proj, view, &winX, &winY, &winZ) == GL_TRUE) {
            if (winZ < 0.0 || winZ > 1.0) continue;
            
            char buf[32];
            sprintf(buf, "[%d,%d,%d]", faces[i].nq[0], faces[i].nq[1], faces[i].nq[2]);
            
            glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
            gluOrtho2D(0, view[2], 0, view[3]);
            glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
            glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);
            
            glColor3f(0.0f, 0.8f, 0.8f); /* Cyan-ish LCARS */
            glRasterPos2f(winX - 25, winY);
            for(int k=0; k<strlen(buf); k++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, buf[k]);
            
            glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING);
            glMatrixMode(GL_PROJECTION); glPopMatrix();
            glMatrixMode(GL_MODELVIEW); glPopMatrix();
        }
    }
}

void drawTacticalCube() {
    glDisable(GL_LIGHTING);
    glLineWidth(2.0f);
    
    float min = -5.0f;
    float mid = 0.0f;
    float max = 5.0f;

    /* 1. AFT PLANE (Anello Posteriore - Rosso) - Facing Z+ (Viewer) */
    glColor3f(1.0f, 0.0f, 0.0f);
    glBegin(GL_LINE_LOOP);
    glVertex3f(min, min, max); glVertex3f(max, min, max);
    glVertex3f(max, max, max); glVertex3f(min, max, max);
    glEnd();

    /* 2. FORWARD PLANE (Anello Frontale - Verde) - Facing Z- (Viewer) */
    glColor3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_LINE_LOOP);
    glVertex3f(min, min, min); glVertex3f(max, min, min);
    glVertex3f(max, max, min); glVertex3f(min, max, min);
    glEnd();

    /* 3. LONGITUDINAL CONNECTORS (Sfumati Rosso -> Giallo -> Verde) */
    glBegin(GL_LINES);
    for(int i=0; i<4; i++) {
        float x = (i==0 || i==3) ? min : max;
        float y = (i<2) ? min : max;
        
        /* Parte posteriore -> centro (Rosso -> Giallo) */
        glColor3f(1.0f, 0.0f, 0.0f); glVertex3f(x, y, max);
        glColor3f(1.0f, 1.0f, 0.0f); glVertex3f(x, y, mid);
        
        /* Parte centro -> anteriore (Giallo -> Verde) */
        glColor3f(1.0f, 1.0f, 0.0f); glVertex3f(x, y, mid);
        glColor3f(0.0f, 1.0f, 0.0f); glVertex3f(x, y, min);
    }
    glEnd();

    glEnable(GL_LIGHTING);
}

void display() {
    if (g_data_dirty) { loadGameState(); g_data_dirty = 0; }
    
    /* RESET STACKS */
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(45, 1.33, 0.1, 500); 
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    /* Cinematic Camera Transition */
    float target_zoom = -35.0f; /* Map Zoom */
    float current_zoom = zoom * (1.0f - map_anim) + target_zoom * map_anim;
    
    glTranslatef(0, 0, current_zoom);
    glRotatef(angleX, 1, 0, 0); 
    glRotatef(angleY, 0, 1, 0);

    /* Sky color pulse */
    float sn_intensity = 0.0f;
    if (g_shared_state->shm_galaxy[g_shared_state->shm_q[0]][g_shared_state->shm_q[1]][g_shared_state->shm_q[2]] < 0) {
        int timer = -g_shared_state->shm_galaxy[g_shared_state->shm_q[0]][g_shared_state->shm_q[1]][g_shared_state->shm_q[2]];
        sn_intensity = 0.3f + sin(pulse*10.0f) * 0.2f;
        if (timer < 300) sn_intensity += 0.3f; 
    }
    
    /* Background fade to darker black in map mode */
    float bg_level = 0.05f * (1.0f - map_anim);
    glClearColor(bg_level + sn_intensity, bg_level, bg_level, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* --- RENDER GALAXY MAP (Fades In) --- */
    if (map_anim > 0.01f) {
        glPushMatrix();
        /* Holographic appearing effect: Scale up from center */
        float map_scale = map_anim; 
        glScalef(map_scale, map_scale, map_scale);
        drawGalaxyMap();
        glPopMatrix();
    }

    /* --- RENDER TACTICAL VIEW (Fades Out) --- */
    if (map_anim < 0.99f) {
        glPushMatrix();
        float tact_scale = 1.0f - map_anim;
        glScalef(tact_scale, tact_scale, tact_scale);

        /* 1. BACKGROUND STARS */
        glDisable(GL_LIGHTING);
        if (vbo_stars != 0) { 
            glPointSize(1.0f);
            glColor3f(0.8f, 0.8f, 0.8f); 
            glEnableClientState(GL_VERTEX_ARRAY); 
            glBindBuffer(GL_ARRAY_BUFFER, vbo_stars); 
            glVertexPointer(3, GL_FLOAT, 0, 0); 
            glDrawArrays(GL_POINTS, 0, 1000); 
            glBindBuffer(GL_ARRAY_BUFFER, 0); 
            glDisableClientState(GL_VERTEX_ARRAY); 
        }

        drawTacticalCube();

        /* 2. LOCAL OBJECTS */
        if (g_show_axes) {
            glPushMatrix();
            glTranslatef(objects[0].x, objects[0].y, objects[0].z);
            drawCompass();
            glPopMatrix();
        }
        if (g_show_grid) drawGrid();
        
        drawPhaserBeams();
        drawExplosion();
        drawJumpArrival();
        drawTorpedo();
        if (g_wormhole.active && g_jump_arrival.timer <= 0) drawWormhole(g_wormhole.x, g_wormhole.y, g_wormhole.z, g_wormhole.h, g_wormhole.m, 0);
        drawDismantle();

        /* Supernova Flash */
        if (g_sn_pos.active && g_sn_q[0] == g_my_q[0] && g_sn_q[1] == g_my_q[1] && g_sn_q[2] == g_my_q[2] && g_sn_pos.timer < 30) {
            glDisable(GL_LIGHTING);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            float flash_s = (30 - g_sn_pos.timer) * 0.5f;
            glColor4f(1.0f, 1.0f, 1.0f, 0.8f);
            glPushMatrix();
            glTranslatef(g_sn_pos.x, g_sn_pos.y, g_sn_pos.z);
            glutSolidSphere(flash_s, 32, 32);
            glPopMatrix();
            glDisable(GL_BLEND);
            glEnable(GL_LIGHTING);
        }

        /* Trails */
        for(int k=0; k<200; k++) {
            if (objects[k].type == 1 || objects[k].type >= 10) drawShipTrail(k);
        }

        /* Objects */
        glEnable(GL_LIGHTING);
        for(int i=0; i<200; i++) {
            if (objects[i].type == 0) continue;

            glPushMatrix(); glTranslatef(objects[i].x, objects[i].y, objects[i].z);
            if (objects[i].type == 1) { 
                if (i == 0 && g_shared_state->is_cloaked) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glColor4f(0.5f, 0.8f, 1.0f, 0.3f); }
                drawFederationShip(objects[i].ship_class, objects[i].h, objects[i].m);
                if (i == 0 && g_shared_state->is_cloaked) glDisable(GL_BLEND);
            } else {
                glRotatef(objects[i].h - 90.0f, 0, 1, 0); glRotatef(objects[i].m, 0, 0, 1);
                switch(objects[i].type) {
                    case 3: drawStarbase(0,0,0); break;
                    case 4: drawStar(objects[i].x, objects[i].y, objects[i].z, objects[i].id); break;
                    case 5: drawPlanet(0,0,0); break;
                    case 6: drawBlackHole(0,0,0); break;
                    case 7: drawStellarNebula(0,0,0); break;
                    case 8: drawPulsar(0,0,0); break;
                    case 9: drawComet(0,0,0); break;
                    case 10: drawKlingon(0,0,0); break;
                    case 21: drawAsteroid(0,0,0); break;
                    case 22: drawDerelict(objects[i].ship_class); break;
                    case 23: drawMine(0,0,0); break;
                    case 24: drawBuoy(0,0,0); break;
                    case 25: drawPlatform(0,0,0); break;
                    case 26: drawRift(0,0,0); break;
                    case 30: drawMonster(30,0,0,0); break;
                    case 31: drawMonster(31,0,0,0); break;
                    case 11: drawRomulan(0,0,0); break;
                    case 12: drawBorg(0,0,0); break;
                    case 13: drawCardassian(0,0,0); break;
                    case 14: drawJemHadar(0,0,0); break;
                    case 15: drawTholian(0,0,0); break;
                    case 16: drawGorn(0,0,0); break;
                    case 17: drawFerengi(0,0,0); break;
                    case 18: drawSpecies8472(0,0,0); break;
                    case 19: drawBreen(0,0,0); break;
                    case 20: drawHirogen(0,0,0); break;
                }
            }
            glPopMatrix();
        }

        drawFaceLabels();

        /* HUD Overlay (Tactical Mode) - Fade Alpha */
        if (g_show_hud) {
            if (map_anim < 0.5f) {
                for(int i=0; i<200; i++) {
                    if (objects[i].type != 0 && !g_is_loading) {
                        drawHUD(i);
                    }
                }
            }
        }
        glPopMatrix(); /* End of Tactical Mode scaling */
    }
    
    /* Draw HUD Overlay (Map Mode) */
    if (g_show_hud && map_anim > 0.5f) {
        /* Show Map specific text */
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); gluOrtho2D(0, 1000, 0, 1000); glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glDisable(GL_LIGHTING); glColor3f(1, 1, 0);
        drawText3D(20, 960, 0, "--- STELLAR CARTOGRAPHY: FULL GALAXY VIEW ---");
        drawText3D(20, 935, 0, "RED: Hostiles | GREEN: Bases | CYAN: Planets | PURPLE: Black Holes | YELLOW: Stars");
        drawText3D(20, 910, 0, "GREY: Nebulas | ORANGE: Pulsars | WHITE SHELL: Ion Storms");
        drawText3D(20, 885, 0, "LIGHT BLUE: Comets | BROWN: Asteroid Fields");
        drawText3D(20, 860, 0, "DARK GREY: Derelict Ships");
        drawText3D(20, 835, 0, "BRIGHT RED: Hostile Minefields");
        drawText3D(20, 810, 0, "BLUE: Federation Comm Buoys");
        drawText3D(20, 785, 0, "ORANGE: Defense Platforms");
        drawText3D(20, 760, 0, "CYAN: Spatial Rifts (Teleport)");
        drawText3D(20, 735, 0, "WHITE: Space Monsters (BOSS)");
        glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix();
    }

    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); gluOrtho2D(0, 1000, 0, 1000); glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glDisable(GL_LIGHTING); glColor3f(0, 1, 1); 
    if (g_show_map) drawText3D(20, 50, 0, "Arrows: Rotate Map | W/S: Zoom Map | map (in CLI): Exit Map Mode");
    else drawText3D(20, 50, 0, "Arrows: Rotate | W/S: Zoom | H: Toggle HUD | map (in CLI): Enter Map Mode | ESC: Exit");

    char buf[256]; 
    if (g_show_hud && map_anim < 0.5f) {
        /* --- TOP LEFT: Comprehensive Ship Status --- */
        int x_off = 20;
        int y_pos = 970;
        
        /* 1. Command & Location */
        glColor3f(1.0f, 1.0f, 0.0f); /* Yellow */
        /* Use captain name and class from local variables copied under mutex */
        sprintf(buf, "%s - CAPTAIN: %s", getClassName(g_player_class), g_player_name);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 20;

        glColor3f(0.0f, 1.0f, 1.0f); /* Cyan */
        /* Convert focal point coordinates back to Trek sector coordinates (0 to 10) */
        float disp_s1 = enterpriseX + 5.0f;
        float disp_s2 = 5.0f - enterpriseZ;
        float disp_s3 = enterpriseY + 5.0f;
        sprintf(buf, "QUADRANT: %s  |  SECTOR: [%.2f, %.2f, %.2f]", g_quadrant, disp_s1, disp_s2, disp_s3); 
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 25;

        /* 2. Vital Resources */
        glColor3f(1.0f, 1.0f, 1.0f);
        sprintf(buf, "ENERGY: %-7d (CARGO: %-7d) | TORPS: %-4d (CARGO: %-4d)", g_energy, g_cargo_energy, g_torpedoes_launcher, g_cargo_torps);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 18;
        
        /* Hull Integrity Main Display */
        if (g_hull_integrity > 60) glColor3f(0, 1, 0);
        else if (g_hull_integrity > 25) glColor3f(1, 1, 0);
        else glColor3f(1, 0, 0);
        sprintf(buf, "HULL INTEGRITY: %.1f%%", g_hull_integrity);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 18;

        if (g_duranium_plating > 0) {
            glColor3f(1.0f, 0.8f, 0.0f);
            sprintf(buf, "HULL PLATING: %-5d [DURANIUM REINFORCED]", g_duranium_plating);
            drawText3D(x_off, y_pos, 0, buf); y_pos -= 18;
        }

        glColor3f(1.0f, 1.0f, 1.0f);
        sprintf(buf, "CREW:   %-4d   SHIELDS AVG: %-3d%%      | LOCK:  ", g_crew, g_shields);
        drawText3D(x_off, y_pos, 0, buf);
        if (g_lock_target > 0) {
            glColor3f(1, 0, 0); sprintf(buf, "[ ID %d ]", g_lock_target);
        } else {
            glColor3f(0.5, 0.5, 0.5); sprintf(buf, "[ NONE ]");
        }
        drawText3D(x_off + 260, y_pos, 0, buf); y_pos -= 20;

        /* 2.1 Individual Shields */
        glColor3f(0.0f, 0.7f, 1.0f);
        const char* sh_names[] = {"F:", "R:", "T:", "B:", "L:", "RI:"};
        for(int i=0; i<6; i++) {
            sprintf(buf, "%s %-4d", sh_names[i], g_shields_val[i]);
            drawText3D(x_off + i*60, y_pos, 0, buf);
        }
        y_pos -= 25;

        /* 3. System Health (2 columns) */
        glColor3f(0.0f, 0.8f, 0.0f);
        drawText3D(x_off, y_pos, 0, "--- SYSTEMS HEALTH ---"); y_pos -= 18;
        const char* sys_names[] = {"Warp", "Impulse", "Sensors", "Transp", "Phasers", "Torps", "Computer", "Life", "Shields", "Aux"};
        for(int i=0; i<5; i++) {
            for(int col=0; col<2; col++) {
                int idx = i + col*5;
                float h = g_system_health[idx];
                if (h > 75) glColor3f(0, 1, 0); else if (h > 30) glColor3f(1, 1, 0); else glColor3f(1, 0, 0);
                sprintf(buf, "%-8s: %3.0f%%", sys_names[idx], h);
                drawText3D(x_off + col*150, y_pos, 0, buf);
            }
            y_pos -= 15;
        }
        y_pos -= 10;

        /* 4. Cargo Inventory (2 columns) */
        glColor3f(0.8f, 0.5f, 0.0f);
        drawText3D(x_off, y_pos, 0, "--- CARGO INVENTORY ---"); y_pos -= 18;
        const char* res_names[] = {"-", "Dilithium", "Tritanium", "Verterium", "Monotanium", "Isolinear", "Gases", "Duranium", "Prisoners"};
        for(int i=1; i<5; i++) {
            for(int col=0; col<2; col++) {
                int idx = i + col*4;
                if (idx > 8) continue;
                glColor3f(0.7, 0.7, 0.7);
                sprintf(buf, "%-10s: %-4d", res_names[idx], g_inventory[idx]);
                drawText3D(x_off + col*150, y_pos, 0, buf);
            }
            y_pos -= 15;
        }
        y_pos -= 10;

        /* 5. Reactor Power Distribution */
        glColor3f(1.0f, 1.0f, 0.0f);
        drawText3D(x_off, y_pos, 0, "--- REACTOR POWER ALLOCATION ---"); y_pos -= 18;
        sprintf(buf, "ENGINES: %d%%  |  SHIELDS: %d%%  |  WEAPONS: %d%%", 
                (int)(g_shared_state->shm_power_dist[0]*100), 
                (int)(g_shared_state->shm_power_dist[1]*100), 
                (int)(g_shared_state->shm_power_dist[2]*100));
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 25;

        /* 6. Tactical Ordnance & Defense */
        glColor3f(1.0f, 0.0f, 0.0f);
        drawText3D(x_off, y_pos, 0, "--- TACTICAL ORDNANCE ---"); y_pos -= 18;
        
        /* Weapons Status */
        glColor3f(1.0f, 0.5f, 0.5f);
        const char* tube_labels[] = {"READY", "FIRING...", "LOADING...", "OFFLINE"};
        int ts = g_shared_state->shm_tube_state; if(ts<0||ts>3) ts=3;
        sprintf(buf, "PHASER CAPACITOR: %-3.0f%% | TUBES: %s", 
                g_shared_state->shm_phaser_charge,
                tube_labels[ts]);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 15;
        
        sprintf(buf, "PHASER INTEGRITY: %-3.0f%%  | CORBOMITE: %d", 
                g_system_health[4],
                g_shared_state->shm_corbomite);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 15;
        
        sprintf(buf, "LIFE SUPPORT: %.1f%%", g_shared_state->shm_life_support);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 25;

        /* 7. Target Tactical Overlay (Center Screen) */
        if (g_lock_target > 0) {
            int tx_pos = 400;
            int ty_pos = 150;
            glColor3f(1.0f, 0.0f, 0.0f);
            drawText3D(tx_pos, ty_pos, 0, ">>> TARGET LOCKED <<<"); ty_pos -= 20;
            
            /* Find target data */
            for(int i=0; i<200; i++) {
                if (objects[i].id == g_lock_target) {
                    glColor3f(1.0f, 1.0f, 1.0f);
                    
                    const char* f_name = "Neutral/Unknown";
                    switch(objects[i].faction) {
                        case 0:  f_name = "FEDERATION"; glColor3f(0, 1, 1); break;
                        case 10: f_name = "KLINGON"; glColor3f(1, 0, 0); break;
                        case 11: f_name = "ROMULAN"; glColor3f(0, 1, 0); break;
                        case 12: f_name = "BORG"; glColor3f(1, 0, 1); break;
                        case 13: f_name = "CARDASSIAN"; glColor3f(1, 0.5, 0); break;
                        case 14: f_name = "DOMINION"; glColor3f(0.5, 0, 1); break;
                        default: f_name = "INDEPENDENT"; glColor3f(0.8, 0.8, 0.8); break;
                    }

                    sprintf(buf, "NAME: %s (%s)", objects[i].name, f_name);
                    drawText3D(tx_pos, ty_pos, 0, buf); ty_pos -= 15;
                    
                    float dx = objects[i].x - enterpriseX;
                    float dy = objects[i].y - enterpriseY;
                    float dz = objects[i].z - enterpriseZ;
                    float dist = sqrt(dx*dx + dy*dy + dz*dz);
                    
                    glColor3f(1.0f, 1.0f, 1.0f);
                    sprintf(buf, "ENERGY: %d (%d%%) | DIST: %.2f", objects[i].energy, objects[i].health_pct, dist);
                    drawText3D(tx_pos, ty_pos, 0, buf); ty_pos -= 15;

                    sprintf(buf, "HEADING: %.0f | MARK: %+.0f", objects[i].h, objects[i].m);
                    drawText3D(tx_pos, ty_pos, 0, buf);
                    break;
                }
            }
        }

        if (g_shared_state->is_cloaked) {
            glColor3f(0.5f, 0.5f, 1.0f);
            drawText3D(x_off, y_pos - 20, 0, ">>> CLOAKING DEVICE ACTIVE <<<");
        }

        /* --- TOP RIGHT: Quadrant Object List --- */
        int y_off = 965;
        glColor3f(1.0f, 0.5f, 0.0f);
        drawText3D(750, y_off, 0, "--- QUADRANT SENSORS ---");
        y_off -= 25;
        
        for(int i=0; i<200; i++) {
            if (objects[i].id != 0 && objects[i].type != 0) {
                if (objects[i].type == 1) glColor3f(0, 1, 1);
                else if (objects[i].type >= 10 && objects[i].type <= 20) glColor3f(1, 0, 0);
                else glColor3f(0.8, 0.8, 0.8);

                const char* t_name = "Object";
                switch(objects[i].type) {
                    case 1: t_name = "PLAYER"; break;
                    case 3: t_name = "BASE"; break;
                    case 4: t_name = "STAR"; break;
                    case 5: t_name = "PLANET"; break;
                    case 6: t_name = "BLACKHOLE"; break;
                    case 10: t_name = "KLINGON"; break;
                    case 11: t_name = "ROMULAN"; break;
                    case 12: t_name = "BORG"; break;
                    default: t_name = "OTHER"; break;
                }
                
                if (objects[i].type == 1) sprintf(buf, "[%03d] %s", objects[i].id, objects[i].name);
                else if (objects[i].type == 4) {
                    const char* st_cls[] = {"O", "B", "A", "F", "G", "K", "M"};
                    int c_idx = objects[i].ship_class; if(c_idx<0)c_idx=0; if(c_idx>6)c_idx=6;
                    sprintf(buf, "[%03d] STAR: Class %s", objects[i].id, st_cls[c_idx]);
                } else if (objects[i].type == 5) {
                    const char* p_res[] = {"None", "Dil", "Tri", "Ver", "Per", "Anti", "Xen"};
                    int r_idx = objects[i].ship_class; if(r_idx<0)r_idx=0; if(r_idx>6)r_idx=6;
                    sprintf(buf, "[%03d] PLANET: %s", objects[i].id, p_res[r_idx]);
                } else if (objects[i].type == 7) {
                    const char* n_cls[] = {"Mutara", "Paulson", "Mar Oscura", "McAllister", "Arachnia"};
                    int n_idx = objects[i].ship_class; if(n_idx<0)n_idx=0; if(n_idx>4)n_idx=4;
                    sprintf(buf, "[%03d] NEBULA: %s", objects[i].id, n_cls[n_idx]);
                }
                else sprintf(buf, "[%03d] %s (%s)", objects[i].id, objects[i].name, t_name);
                
                drawText3D(750, y_off, 0, buf);
                y_off -= 20;
                if (y_off < 500) {
                    drawText3D(750, y_off, 0, "...");
                    break;
                }
            }
        }

        /* --- BOTTOM RIGHT: Subspace Telemetry --- */
        int ty = 120;
        glColor3f(0.0f, 0.8f, 1.0f); /* LCARS Blue */
        drawText3D(750, ty, 0, "--- SUBSPACE UPLINK DIAGNOSTICS ---"); ty -= 20;
        
        glColor3f(0.0f, 0.5f, 0.7f);
        sprintf(buf, "LINK UPTIME: %02ld:%02ld:%02ld", g_shared_state->net_uptime/3600, (g_shared_state->net_uptime%3600)/60, g_shared_state->net_uptime%60);
        drawText3D(750, ty, 0, buf); ty -= 15;
        
        sprintf(buf, "BANDWIDTH: %.2f KB/s | PPS: %d", g_shared_state->net_kbps, g_shared_state->net_packet_count);
        drawText3D(750, ty, 0, buf); ty -= 15;
        
        sprintf(buf, "PULSE JITTER: %.2f ms", g_shared_state->net_jitter);
        drawText3D(750, ty, 0, buf); ty -= 15;
        
        sprintf(buf, "SIGNAL INTEGRITY: %.1f%%", g_shared_state->net_integrity);
        drawText3D(750, ty, 0, buf); ty -= 15;

        glColor3f(1.0f, 1.0f, 0.0f);
        sprintf(buf, "POWER: E:%d%% S:%d%% W:%d%%", 
                (int)(g_shared_state->shm_power_dist[0]*100), 
                (int)(g_shared_state->shm_power_dist[1]*100), 
                (int)(g_shared_state->shm_power_dist[2]*100));
        drawText3D(750, ty, 0, buf); ty -= 15;

        glColor3f(0.0f, 0.5f, 0.7f);
        sprintf(buf, "AVG FRAME: %d bytes (Opt: %.1f%%)", g_shared_state->net_avg_packet_size, g_shared_state->net_efficiency);
        drawText3D(750, ty, 0, buf); ty -= 15;

        if (g_shared_state->shm_crypto_algo == CRYPTO_AES) {
            glColor3f(0.0f, 1.0f, 0.0f);
            drawText3D(750, ty, 0, "ENCRYPTION: AES-256-GCM ACTIVE");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_CHACHA) {
            glColor3f(0.0f, 1.0f, 0.5f);
            drawText3D(750, ty, 0, "ENCRYPTION: CHACHA20-POLY ACTIVE");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_ARIA) {
            glColor3f(0.0f, 0.7f, 1.0f);
            drawText3D(750, ty, 0, "ENCRYPTION: ARIA-256-GCM ACTIVE");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_CAMELLIA) {
            glColor3f(0.0f, 1.0f, 0.0f);
            drawText3D(750, ty, 0, "ENCRYPTION: CAMELLIA-256 (ROMULAN)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_SEED) {
            glColor3f(1.0f, 0.5f, 0.0f);
            drawText3D(750, ty, 0, "ENCRYPTION: SEED-CBC (ORION)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_CAST5) {
            glColor3f(1.0f, 1.0f, 0.0f);
            drawText3D(750, ty, 0, "ENCRYPTION: CAST5-CBC (REPUBLIC)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_IDEA) {
            glColor3f(1.0f, 0.0f, 1.0f);
            drawText3D(750, ty, 0, "ENCRYPTION: IDEA-CBC (MAQUIS)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_3DES) {
            glColor3f(0.5f, 0.5f, 0.5f);
            drawText3D(750, ty, 0, "ENCRYPTION: 3DES-CBC (ANCIENT)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_BLOWFISH) {
            glColor3f(0.7f, 0.4f, 0.0f);
            drawText3D(750, ty, 0, "ENCRYPTION: BLOWFISH-CBC (FERENGI)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_RC4) {
            glColor3f(0.0f, 0.5f, 0.7f);
            drawText3D(750, ty, 0, "ENCRYPTION: RC4-STREAM (TACTICAL)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_DES) {
            glColor3f(0.4f, 0.4f, 0.4f);
            drawText3D(750, ty, 0, "ENCRYPTION: DES-CBC (PRE-WARP)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_PQC) {
            glColor3f(1.0f, 1.0f, 1.0f);
            drawText3D(750, ty, 0, "ENCRYPTION: ML-KEM-1024 (QUANTUM-SECURE)");
        } else {
            glColor3f(1.0f, 0.0f, 0.0f);
            drawText3D(750, ty, 0, "ENCRYPTION: DISABLED / RAW");
        }
    }

    long long sn_val = g_shared_state->shm_galaxy[g_my_q[0]][g_my_q[1]][g_my_q[2]];
    if (g_show_hud && (g_sn_pos.active || sn_val < 0)) {
        /* Supernova Overlay - Centered and prominently Red */
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); gluOrtho2D(0, 1000, 0, 1000); 
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);
        
        int sec = 0;
        /* Priority 1: Global event timer from server */
        if (g_sn_pos.active && g_sn_pos.timer > 0) {
            sec = g_sn_pos.timer / 30;
        } 
        /* Priority 2: Fallback to grid-encoded timer (only if valid: 1-1800 ticks) */
        else if (sn_val < 0 && sn_val > -5000) {
            sec = (int)(-sn_val / 30);
        }
        
        if (sec > 60) sec = 60; /* Max supernova countdown is 60s */
        if (sec < 1 && (g_sn_pos.active || (sn_val < 0 && sn_val > -5000))) sec = 1;
        
        char sn_buf[128];
        glColor3f(1.0f, 0.0f, 0.0f); /* Bright Red */
        /* Determine if the supernova is IN the current quadrant */
        bool in_this_q = false;
        if (sn_val < 0 && sn_val > -5000) in_this_q = true;
        if (g_sn_pos.active && g_my_q[0] == g_sn_q[0] && g_my_q[1] == g_sn_q[1] && g_my_q[2] == g_sn_q[2]) in_this_q = true;

        if (in_this_q) {
            sprintf(sn_buf, "!!! CRITICAL: SUPERNOVA IMMINENT IN THIS SECTOR: %d SEC !!!", sec);
        } else if (g_sn_pos.active) {
            sprintf(sn_buf, "!!! WARNING: SUPERNOVA DETECTED IN Q-%d-%d-%d: %d SEC !!!", g_sn_q[0], g_sn_q[1], g_sn_q[2], sec);
        } else {
            /* Event likely cleared but grid not yet synced */
            glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix();
            glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING);
            return;
        }
        drawText3D(200, 500, 0, sn_buf);
        
        glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING);
        glMatrixMode(GL_PROJECTION); glPopMatrix(); 
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
    }

    glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glEnable(GL_LIGHTING); glutSwapBuffers();
}

void timer(int v) { 
    if (autoRotate > 5.0f) autoRotate = 0.5f; /* Cap speed */
    angleY += autoRotate; 
    if (angleY >= 360.0f) angleY -= 360.0f;
    pulse += 0.05; 
    
    /* Cinematic Transition Logic */
    if (g_show_map) {
        if (map_anim < 1.0f) map_anim += 0.04f;
        if (map_anim > 1.0f) map_anim = 1.0f;
    } else {
        if (map_anim > 0.0f) map_anim -= 0.04f;
        if (map_anim < 0.0f) map_anim = 0.0f;
    }

    /* Fade out beams */
    for (int i = 0; i < 10; i++) if (beams[i].alpha > 0) beams[i].alpha -= 0.05f;

    /* Update Boom Timer */
    if (g_boom.timer > 0) g_boom.timer--;
    
    /* Update Supernova timer */
    if (g_sn_pos.active && g_sn_pos.timer > 0) g_sn_pos.timer--;

    /* Update Jump Arrival Timer */
    if (g_jump_arrival.timer > 0) {
        g_jump_arrival.timer--;
        for(int i=0; i<150; i++) {
            if (g_arrival_fx.particles[i].active) {
                g_arrival_fx.particles[i].x += g_arrival_fx.particles[i].vx;
                g_arrival_fx.particles[i].y += g_arrival_fx.particles[i].vy;
                g_arrival_fx.particles[i].z += g_arrival_fx.particles[i].vz;
                
                /* Slowly fade or change behavior if they reach center? 
                   For now just simple convergence */
            }
        }
    }

    /* Update Dismantle */
    if (g_dismantle.timer > 0) {
        g_dismantle.timer--;
        for(int i=0; i<100; i++) {
            if (g_dismantle.particles[i].active) {
                g_dismantle.particles[i].x += g_dismantle.particles[i].vx;
                g_dismantle.particles[i].y += g_dismantle.particles[i].vy;
                g_dismantle.particles[i].z += g_dismantle.particles[i].vz;
            }
        }
    }

    /* Update Objects with Interpolation (GLIDE EFFECT) */
    for (int i = 0; i < 200; i++) {
        if (objects[i].id == 0) continue;
        /* Interpolazione fluida per la posizione (LERP) - Sped up for better glide */
        float interp_speed = 0.35f;
        objects[i].x += (objects[i].tx - objects[i].x) * interp_speed;
        objects[i].y += (objects[i].ty - objects[i].y) * interp_speed;
        objects[i].z += (objects[i].tz - objects[i].z) * interp_speed;
        
        /* Interpolazione fluida per l'orientamento (Heading/Mark) - Significant speed up */
        float dh = objects[i].th - objects[i].h;
        if (dh > 180.0f) dh -= 360.0f;
        if (dh < -180.0f) dh += 360.0f;
        objects[i].h += dh * 0.15f;
        if (objects[i].h >= 360.0f) objects[i].h -= 360.0f;
        if (objects[i].h < 0.0f) objects[i].h += 360.0f;

        objects[i].m += (objects[i].tm - objects[i].m) * 0.15f;
        
        if (objects[i].type == 1 || objects[i].type >= 10) {
            /* Check for jump only if we have a history */
            if (objects[i].trail_count > 0) {
                float lastX = objects[i].trail[(objects[i].trail_ptr - 1 + MAX_TRAIL) % MAX_TRAIL][0];
                float lastY = objects[i].trail[(objects[i].trail_ptr - 1 + MAX_TRAIL) % MAX_TRAIL][1];
                float lastZ = objects[i].trail[(objects[i].trail_ptr - 1 + MAX_TRAIL) % MAX_TRAIL][2];

                /* Rilevamento salto (cambio quadrante o teletrasporto) */
                float dx = objects[i].x - lastX;
                float dy = objects[i].y - lastY;
                float dz = objects[i].z - lastZ;
                float dist_sq = dx*dx + dy*dy + dz*dz;
                if (dist_sq > 25.0f) {
                    objects[i].trail_count = 0;
                    objects[i].trail_ptr = 0;
                }
            }

            static int trail_tick = 0;
            if (trail_tick % 2 == 0) { /* Riduciamo la densità della scia per fluidità visiva */
                objects[i].trail[objects[i].trail_ptr][0] = objects[i].x;
                objects[i].trail[objects[i].trail_ptr][1] = objects[i].y;
                objects[i].trail[objects[i].trail_ptr][2] = objects[i].z;
                objects[i].trail_ptr = (objects[i].trail_ptr + 1) % MAX_TRAIL;
                if (objects[i].trail_count < MAX_TRAIL) objects[i].trail_count++;
            }
        }
    }
    static int global_trail_tick = 0;
    global_trail_tick++;

    glutPostRedisplay(); glutTimerFunc(16, timer, 0); 
}
void keyboard(unsigned char k, int x, int y) { 
    if(k==27) exit(0); 
    if(k==' ') autoRotate=(autoRotate==0)?0.15:0; 
    if(k=='w' || k=='W') zoom += 0.5f;
    if(k=='s' || k=='S') zoom -= 0.5f;
    if(k=='h' || k=='H') g_show_hud = !g_show_hud;
}
void special(int k, int x, int y) { 
    if(k==GLUT_KEY_UP) angleX-=2.5f; 
    if(k==GLUT_KEY_DOWN) angleX+=2.5f; 
    if(angleX > 85.0f) angleX = 85.0f;
    if(angleX < -85.0f) angleX = -85.0f;
    if(k==GLUT_KEY_LEFT) angleY-=5; 
    if(k==GLUT_KEY_RIGHT) angleY+=5; 
}

int main(int argc, char** argv) {

    setlocale(LC_ALL, "C"); signal(SIGUSR1, handle_signal);

    memset(objects, 0, sizeof(objects));

    for(int i=0; i<200; i++) { objects[i].x = objects[i].y = objects[i].z = -100.0f; }

    printf("[3D VIEW] Starting...\n");

    char *shm_name = SHM_NAME; if (argc > 1) shm_name = argv[1];

    printf("[3D VIEW] Connecting to SHM: %s\n", shm_name);
    

    int retries = 0; 

    while(shm_fd == -1 && retries < 10) { 
        shm_fd = shm_open(shm_name, O_RDWR, 0666); 
        if (shm_fd == -1) { 
            printf("[3D VIEW] SHM not ready, retry %d/10...\n", retries+1);
            usleep(100000); retries++; 
        } 
    }

    
    if (shm_fd == -1) {
        fprintf(stderr, "[3D VIEW] FATAL: Could not access shared memory %s after retries.\n", shm_name);
        exit(1);
    }

    g_shared_state = mmap(NULL, sizeof(GameState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (g_shared_state == MAP_FAILED) {
        perror("[3D VIEW] mmap failed");
        exit(1);
    }
        
    printf("[3D VIEW] Shared memory mapped successfully.\n");
    pthread_t stid;
    
    if (pthread_create(&stid, NULL, shm_listener_thread, NULL) != 0) {
        perror("[3D VIEW] Failed to create listener thread");
        exit(1);
    }

    printf("[3D VIEW] Initializing GLUT (check DISPLAY: %s)...\n", getenv("DISPLAY"));
    glutInit(&argc, argv); 
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH); glutInitWindowSize(1024, 768); glutCreateWindow("Trek 3DView - Multiuser");
    
    /* Initialize Shader Engine */
    initShaders();

    glEnable(GL_DEPTH_TEST); 
    glEnable(GL_BLEND); 
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_LIGHTING); 
    glEnable(GL_LIGHT0); 
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_NORMALIZE);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    /* Ambient Light (Shadow fill) */
    GLfloat global_amb[] = {0.2f, 0.2f, 0.25f, 1.0f};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, global_amb);

    /* Main Light (Headlight) */
    GLfloat lp[] = {0, 0, 10, 1}; /* Light coming from camera direction */
    glLightfv(GL_LIGHT0, GL_POSITION, lp);
    GLfloat white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glLightfv(GL_LIGHT0, GL_DIFFUSE, white);
    glLightfv(GL_LIGHT0, GL_SPECULAR, white);
        initStars(); initVBOs(); glMatrixMode(GL_PROJECTION); gluPerspective(45, 1.33, 1, 500); glMatrixMode(GL_MODELVIEW);
        glutDisplayFunc(display); glutKeyboardFunc(keyboard); glutSpecialFunc(special); glutTimerFunc(16, timer, 0);
        
        printf("[3D VIEW] Ready. Sending handshake to parent (PID %d).\n", getppid());
        kill(getppid(), SIGUSR2); 
        glutMainLoop(); 
        return 0;
}
    
