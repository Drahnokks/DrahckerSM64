#pragma once

/**********************
 * COLLISION SETTINGS *
 **********************/

/**
 * Reduces some find_floor calls, at the cost of some barely noticeable smoothness in Mario's visual movement in a few actions at higher speeds.
 * The defined number is the forward speed threshold before the change is active, since it's only noticeable at lower speeds.
 */
#define FAST_FLOOR_ALIGN 10

/**
 * Automatically calculates the optimal collision distance for an object based on its vertices.
 */
#define AUTO_COLLISION_DISTANCE

/**
 * Allows all surfaces types to have force, (doesn't require setting force, just allows it to be optional).
 * Also allows you to pass a warp node to warp floors (SURFACE_WARP, SURFACE_DEATH_PLANE, SURFACE_VERTICAL_WIND) via the second byte of the force parameter.
 */
#define ALL_SURFACES_HAVE_FORCE

/**
 * Number of walls that can push Mario at once. Vanilla is 4.
 */
#define MAX_REFERENCED_WALLS 4

/**
 * When Mario is metal, cancel all the winds effects
 */
#define WIND_RESISTANT_METAL_CAP

/**
 * Collision data is the type that the collision system uses. All data by default is stored as an s16, but you may change it to s32.
 * Naturally, that would double the size of all collision data, but would allow you to use 32 bit values instead of 16.
 * Rooms are s8 in vanilla, but if you somehow have more than 255 rooms, you may raise this number.
 * Currently, they *must* say as s8, because the room tables generated by literally anything are explicitly u8 and don't use a macro, making this currently infeasable.
 */
#define COLLISION_DATA_TYPE s16
#define ROOM_DATA_TYPE s8
