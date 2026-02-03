/* 
 * STARTREK ULTRA - GAME CONFIGURATION
 * Centralized constants for game balance and physics.
 */

#ifndef GAME_CONFIG_H
#define GAME_CONFIG_H

/* --- Resources & Limits --- */
#define MAX_ENERGY_CAPACITY     1000000
#define MAX_TORPEDO_CAPACITY    1000
#define MAX_CREW_GALAXY         1012
#define ENERGY_BASE_RECHARGE    9999999 /* For docking/respawn */

/* --- Combat Mechanics --- */
#define DMG_PHASER_BASE         1500
#define DMG_PHASER_NPC          10      /* NPC base damage per tick */
#define DMG_TORPEDO             75000
#define DMG_TORPEDO_PLATFORM    50000
#define DMG_TORPEDO_MONSTER     100000
#define SHIELD_MAX_STRENGTH     10000
#define SHIELD_REGEN_DELAY      150     /* Ticks before regen after hit */

/* --- Distances (Sector Units) --- */
#define DIST_INTERACTION_MAX    2.0f
#define DIST_MINING_MAX         2.0f
#define DIST_DOCKING_MAX        2.0f
#define DIST_SCOOPING_MAX       2.0f
#define DIST_DISMANTLE_MAX      1.5f
#define DIST_BOARDING_MAX       1.0f
#define DIST_COLLISION_SHIP     0.8f
#define DIST_COLLISION_TORP     0.8f
#define DIST_GRAVITY_WELL       3.0f
#define DIST_EVENT_HORIZON      0.6f

/* --- Timers (Ticks @ 30Hz) --- */
#define TIMER_TORP_LOAD         150     /* 5 Seconds */
#define TIMER_TORP_TIMEOUT      300     /* 10 Seconds */
#define TIMER_SUPERNOVA         1800    /* 60 Seconds */
#define TIMER_WORMHOLE_SEQ      450     /* 15 Seconds */

/* --- Buffer Sizes --- */
#define LARGE_DATA_BUFFER       65536   /* For SRS/LRS scans */

#endif
