#include <PR/ultratypes.h>

#include "sm64.h"
#include "mario_actions_airborne.h"
#include "area.h"
#include "audio/external.h"
#include "camera.h"
#include "engine/graph_node.h"
#include "engine/math_util.h"
#include "game_init.h"
#include "interaction.h"
#include "level_update.h"
#include "mario.h"
#include "mario_step.h"
#include "save_file.h"
#include "rumble_init.h"

#include "config.h"

void play_flip_sounds(struct MarioState *m, s16 frame1, s16 frame2, s16 frame3) {
    s32 animFrame = m->marioObj->header.gfx.animInfo.animFrame;
    if (animFrame == frame1 || animFrame == frame2 || animFrame == frame3) {
        play_sound(SOUND_ACTION_SPIN, m->marioObj->header.gfx.cameraToObject);
    }
}

void play_far_fall_sound(struct MarioState *m) {
    u32 action = m->action;
    if (!(action & ACT_FLAG_INVULNERABLE) && action != ACT_TWIRLING && action != ACT_FLYING
        && !(m->flags & MARIO_FALL_SOUND_PLAYED)) {
        if (m->peakHeight - m->pos[1] > FALL_DAMAGE_HEIGHT_SMALL) {
            play_sound(SOUND_MARIO_WAAAOOOW, m->marioObj->header.gfx.cameraToObject);
            m->flags |= MARIO_FALL_SOUND_PLAYED;
        }
    }
}

void play_knockback_sound(struct MarioState *m) {
    if (m->actionArg == 0 && (m->forwardVel <= -28.0f || m->forwardVel >= 28.0f)) {
        play_sound_if_no_flag(m, SOUND_MARIO_DOH, MARIO_MARIO_SOUND_PLAYED);
    } else {
        play_sound_if_no_flag(m, SOUND_MARIO_UH, MARIO_MARIO_SOUND_PLAYED);
    }
}

s32 lava_boost_on_wall(struct MarioState *m) {
    m->faceAngle[1] = m->wallYaw;

    if (m->forwardVel < 24.0f) {
        m->forwardVel = 24.0f;
    }

    if (!(m->flags & MARIO_METAL_CAP)) {
        m->hurtCounter += (m->flags & MARIO_CAP_ON_HEAD) ? 12 : 18;
    }

    play_sound(SOUND_MARIO_ON_FIRE, m->marioObj->header.gfx.cameraToObject);
    update_mario_sound_and_camera(m);
    return drop_and_set_mario_action(m, ACT_LAVA_BOOST, 1);
}

s32 check_fall_damage(struct MarioState *m, u32 hardFallAction) {
#ifdef NO_FALL_DAMAGE
    return FALSE;
#endif

    f32 fallHeight = m->peakHeight - m->pos[1];

    f32 damageHeight = FALL_DAMAGE_HEIGHT_SMALL;

    if (m->action != ACT_TWIRLING && m->floor->type != SURFACE_BURNING) {
        if (m->vel[1] < -55.0f) {
            if (fallHeight > FALL_DAMAGE_HEIGHT_LARGE) {
                m->hurtCounter += (m->flags & MARIO_CAP_ON_HEAD) ? 16 : 24;
#if ENABLE_RUMBLE
                queue_rumble_data(5, 80);
#endif
                set_camera_shake_from_hit(SHAKE_FALL_DAMAGE);
                play_sound(SOUND_MARIO_ATTACKED, m->marioObj->header.gfx.cameraToObject);
                return drop_and_set_mario_action(m, hardFallAction, 4);
            } else if (fallHeight > damageHeight && !mario_floor_is_slippery(m)) {
                m->hurtCounter += (m->flags & MARIO_CAP_ON_HEAD) ? 8 : 12;
                m->squishTimer = 30;
#if ENABLE_RUMBLE
                queue_rumble_data(5, 80);
#endif
                set_camera_shake_from_hit(SHAKE_FALL_DAMAGE);
                play_sound(SOUND_MARIO_ATTACKED, m->marioObj->header.gfx.cameraToObject);
            }
        }
    }

    return FALSE;
}

s32 check_kick_or_dive_in_air(struct MarioState *m) {
    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, m->forwardVel > 28.0f ? ACT_DIVE : ACT_JUMP_KICK, 0);
    }
    return FALSE;
}

#ifdef NO_GETTING_BURIED
s32 should_get_stuck_in_ground(UNUSED struct MarioState *m) {
    return FALSE;
}
#else
s32 should_get_stuck_in_ground(struct MarioState *m) {
    u32 terrainType = m->area->terrainType & TERRAIN_MASK;
    struct Surface *floor = m->floor;
    s32 flags = floor->flags;
    s32 type = floor->type;

    if (floor != NULL 
        && (terrainType == TERRAIN_SNOW || terrainType == TERRAIN_SAND || type == SURFACE_SAND)
        && type != SURFACE_BURNING && SURFACE_IS_NOT_HARD(type)) {
        if (!(flags & SURFACE_FLAG_DYNAMIC) && m->peakHeight - m->pos[1] > 1000.0f && floor->normal.y >= COS30) {
            return TRUE;
        }
    }

    return FALSE;
}
#endif

s32 check_fall_damage_or_get_stuck(struct MarioState *m, u32 hardFallAction) {
    if (should_get_stuck_in_ground(m)) {
        play_sound(SOUND_MARIO_OOOF2, m->marioObj->header.gfx.cameraToObject);
        m->particleFlags |= PARTICLE_MIST_CIRCLE;
        drop_and_set_mario_action(m, ACT_FEET_STUCK_IN_GROUND, 0);
#if ENABLE_RUMBLE
        queue_rumble_data(5, 80);
#endif
        return TRUE;
    }

    return check_fall_damage(m, hardFallAction);
}

s32 check_horizontal_wind(struct MarioState *m) {
    struct Surface *floor = m->floor;
    f32 speed;
    s16 pushAngle;

#ifdef WIND_RESISTANT_METAL_CAP
    if (floor->type == SURFACE_HORIZONTAL_WIND && !(m->flags & MARIO_METAL_CAP)) {
#else
    if (floor->type == SURFACE_HORIZONTAL_WIND) {
#endif
        pushAngle = floor->force << 8;

        m->slideVelX += 1.2f * sins(pushAngle);
        m->slideVelZ += 1.2f * coss(pushAngle);

        speed = (sqr(m->slideVelX) + sqr(m->slideVelZ));

        if (speed > sqr(48.0f)) {
            speed = sqrtf(speed);
            m->slideVelX = m->slideVelX * 48.0f / speed;
            m->slideVelZ = m->slideVelZ * 48.0f / speed;
            speed = 48.0f;
        } else if (speed > 32.0f) {
            speed = 32.0f;
        }

        m->vel[0] = m->slideVelX;
        m->vel[2] = m->slideVelZ;
        m->slideYaw = atan2s(m->slideVelZ, m->slideVelX);
        m->forwardVel = speed * coss(m->faceAngle[1] - m->slideYaw);
        return TRUE;
    }

    return FALSE;
}

void update_air_with_turn(struct MarioState *m) {
    f32 dragThreshold;
    s16 intendedDYaw;
    f32 intendedMag;

    if (!check_horizontal_wind(m)) {
        dragThreshold = m->action == ACT_LONG_JUMP ? 48.0f : 32.0f;
        m->forwardVel = approach_f32(m->forwardVel, 0.0f, 0.35f, 0.35f);

        if (m->input & INPUT_NONZERO_ANALOG) {
            intendedDYaw = m->intendedYaw - m->faceAngle[1];
            intendedMag = m->intendedMag / 32.0f;

            m->forwardVel += 1.5f * coss(intendedDYaw) * intendedMag;
            m->faceAngle[1] += 512.0f * sins(intendedDYaw) * intendedMag;
        }

        //! Uncapped air speed. Net positive when moving forward.
        if (m->forwardVel > dragThreshold) {
            m->forwardVel -= 1.0f;
        }
        if (m->forwardVel < -16.0f) {
            m->forwardVel += 2.0f;
        }

        m->vel[0] = m->slideVelX = m->forwardVel * sins(m->faceAngle[1]);
        m->vel[2] = m->slideVelZ = m->forwardVel * coss(m->faceAngle[1]);
    }
}

void update_air_without_turn(struct MarioState *m) {
    f32 sidewaysSpeed = 0.0f;
    f32 dragThreshold;
    s16 intendedDYaw;
    f32 intendedMag;

#ifdef KOOPA_SHELL_COYOTE_TIME
    // if mario is falling, press A, and the coyote time isn't over, then jump
    if (m->action == ACT_RIDING_SHELL_FALL && m->input & INPUT_A_PRESSED && m->riddenObj->oCoyoteTimer < KOOPA_SHELL_COYOTE_TIME) {
            return set_mario_action(m, ACT_RIDING_SHELL_JUMP, 0);
        }
#endif

    if (!check_horizontal_wind(m)) {
        dragThreshold = m->action == ACT_LONG_JUMP ? 48.0f : 32.0f;
        m->forwardVel = approach_f32(m->forwardVel, 0.0f, 0.35f, 0.35f);

        if (m->input & INPUT_NONZERO_ANALOG) {
            intendedDYaw = m->intendedYaw - m->faceAngle[1];
            intendedMag = m->intendedMag / 32.0f;

            m->forwardVel += intendedMag * coss(intendedDYaw) * 1.5f;
            sidewaysSpeed = intendedMag * sins(intendedDYaw) * 10.0f;
        }

        //! Uncapped air speed. Net positive when moving forward.
        if (m->forwardVel > dragThreshold) {
            m->forwardVel -= 1.0f;
        }
        if (m->forwardVel < -16.0f) {
            m->forwardVel += 2.0f;
        }

        m->slideVelX = m->forwardVel * sins(m->faceAngle[1]);
        m->slideVelZ = m->forwardVel * coss(m->faceAngle[1]);

        m->slideVelX += sidewaysSpeed * sins(m->faceAngle[1] + 0x4000);
        m->slideVelZ += sidewaysSpeed * coss(m->faceAngle[1] + 0x4000);

        m->vel[0] = m->slideVelX;
        m->vel[2] = m->slideVelZ;
    }
}

void update_lava_boost_or_twirling(struct MarioState *m) {
    s16 intendedDYaw;
    f32 intendedMag;

    if (m->input & INPUT_NONZERO_ANALOG) {
        intendedDYaw = m->intendedYaw - m->faceAngle[1];
        intendedMag = m->intendedMag / 32.0f;

        m->forwardVel += coss(intendedDYaw) * intendedMag;
        m->faceAngle[1] += sins(intendedDYaw) * intendedMag * 1024.0f;

        if (m->forwardVel < 0.0f) {
            m->faceAngle[1] += 0x8000;
            m->forwardVel *= -1.0f;
        }

        if (m->forwardVel > 32.0f) {
            m->forwardVel -= 2.0f;
        }
    }

    m->vel[0] = m->slideVelX = m->forwardVel * sins(m->faceAngle[1]);
    m->vel[2] = m->slideVelZ = m->forwardVel * coss(m->faceAngle[1]);
}

void update_flying_yaw(struct MarioState *m) {
    s16 targetYawVel = -(s16)(m->controller->stickX * (m->forwardVel / 4.0f));

    if (targetYawVel > 0) {
        if (m->angleVel[1] < 0) {
            m->angleVel[1] += 0x40;
            if (m->angleVel[1] > 0x10) {
                m->angleVel[1] = 0x10;
            }
        } else {
            m->angleVel[1] = approach_s32(m->angleVel[1], targetYawVel, 0x10, 0x20);
        }
    } else if (targetYawVel < 0) {
        if (m->angleVel[1] > 0) {
            m->angleVel[1] -= 0x40;
            if (m->angleVel[1] < -0x10) {
                m->angleVel[1] = -0x10;
            }
        } else {
            m->angleVel[1] = approach_s32(m->angleVel[1], targetYawVel, 0x20, 0x10);
        }
    } else {
        m->angleVel[1] = approach_s32(m->angleVel[1], 0, 0x40, 0x40);
    }

    m->faceAngle[1] += m->angleVel[1];
    m->faceAngle[2] = 20 * -m->angleVel[1];
}

void update_flying_pitch(struct MarioState *m) {
    s16 targetPitchVel = -(s16)(m->controller->stickY * (m->forwardVel / 5.0f));

    if (targetPitchVel > 0) {
        if (m->angleVel[0] < 0) {
            m->angleVel[0] += 0x40;
            if (m->angleVel[0] > 0x20) {
                m->angleVel[0] = 0x20;
            }
        } else {
            m->angleVel[0] = approach_s32(m->angleVel[0], targetPitchVel, 0x20, 0x40);
        }
    } else if (targetPitchVel < 0) {
        if (m->angleVel[0] > 0) {
            m->angleVel[0] -= 0x40;
            if (m->angleVel[0] < -0x20) {
                m->angleVel[0] = -0x20;
            }
        } else {
            m->angleVel[0] = approach_s32(m->angleVel[0], targetPitchVel, 0x40, 0x20);
        }
    } else {
        m->angleVel[0] = approach_s32(m->angleVel[0], 0, 0x40, 0x40);
    }
}

void update_flying(struct MarioState *m) {
    update_flying_pitch(m);
    update_flying_yaw(m);

    m->forwardVel -= 2.0f * ((f32) m->faceAngle[0] / 0x4000) + 0.1f;
    m->forwardVel -= 0.5f * (1.0f - coss(m->angleVel[1]));

    if (m->forwardVel < 0.0f) {
        m->forwardVel = 0.0f;
    }

    if (m->forwardVel > 16.0f) {
        m->faceAngle[0] += (m->forwardVel - 32.0f) * 6.0f;
    } else if (m->forwardVel > 4.0f) {
        m->faceAngle[0] += (m->forwardVel - 32.0f) * 10.0f;
    } else {
        m->faceAngle[0] -= 0x400;
    }

    m->faceAngle[0] += m->angleVel[0];

    if (m->faceAngle[0] > DEGREES(60)) {
        m->faceAngle[0] = DEGREES(60);
    }
    if (m->faceAngle[0] < -DEGREES(60)) {
        m->faceAngle[0] = -DEGREES(60);
    }

    m->vel[0] = m->forwardVel * coss(m->faceAngle[0]) * sins(m->faceAngle[1]);
    m->vel[1] = m->forwardVel * sins(m->faceAngle[0]);
    m->vel[2] = m->forwardVel * coss(m->faceAngle[0]) * coss(m->faceAngle[1]);

    m->slideVelX = m->vel[0];
    m->slideVelZ = m->vel[2];
}

u32 common_air_action_step(struct MarioState *m, u32 landAction, s32 animation, u32 stepArg) {
    u32 stepResult;

    update_air_without_turn(m);

    stepResult = perform_air_step(m, stepArg);
    switch (stepResult) {
        case AIR_STEP_NONE:
            set_mario_animation(m, animation);
            break;

        case AIR_STEP_LANDED:
            if (!check_fall_damage_or_get_stuck(m, ACT_HARD_BACKWARD_GROUND_KB)) {
                set_mario_action(m, landAction, 0);
            }
            break;

        case AIR_STEP_HIT_WALL:
            set_mario_animation(m, animation);

            if (m->forwardVel > 16.0f) {
#if ENABLE_RUMBLE
                queue_rumble_data(5, 40);
#endif
                mario_bonk_reflection(m, FALSE);
                m->faceAngle[1] += 0x8000;

                if (m->wall != NULL) {
                    set_mario_action(m, ACT_AIR_HIT_WALL, 0);
                } else {
                    if (m->vel[1] > 0.0f) {
                        m->vel[1] = 0.0f;
                    }

                    //! Hands-free holding. Bonking while no wall is referenced
                    // sets Mario's action to a non-holding action without
                    // dropping the object, causing the hands-free holding
                    // glitch. This can be achieved using an exposed ceiling,
                    // out of bounds, grazing the bottom of a wall while
                    // falling such that the final quarter step does not find a
                    // wall collision, or by rising into the top of a wall such
                    // that the final quarter step detects a ledge, but you are
                    // not able to ledge grab it.
                    // change set_mario_action to drop_and_set_mario_action
                    // in both conditions to fix this
                    if (m->forwardVel >= 38.0f) {
                        m->particleFlags |= PARTICLE_VERTICAL_STAR;
                        set_mario_action(m, ACT_BACKWARD_AIR_KB, 0);
                    } else {
                        if (m->forwardVel > 8.0f) {
                            mario_set_forward_vel(m, -8.0f);
                        }
                        return set_mario_action(m, ACT_SOFT_BONK, 0);
                    }
                }
            } else {
                mario_set_forward_vel(m, 0.0f);
            }
            break;

        case AIR_STEP_GRABBED_LEDGE:
            set_mario_animation(m, MARIO_ANIM_IDLE_ON_LEDGE);
            drop_and_set_mario_action(m, ACT_LEDGE_GRAB, 0);
            break;

        case AIR_STEP_GRABBED_CEILING:
            set_mario_action(m, ACT_START_HANGING, 0);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    return stepResult;
}

s32 act_jump(struct MarioState *m) {
    if (check_kick_or_dive_in_air(m)) {
        return TRUE;
    }

    if (m->input & INPUT_Z_PRESSED) {
        return set_mario_action(m, ACT_GROUND_POUND, 0);
    }

    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, 0);
    common_air_action_step(m, ACT_JUMP_LAND, MARIO_ANIM_SINGLE_JUMP,
                           AIR_STEP_CHECK_LEDGE_GRAB | AIR_STEP_CHECK_HANG);
    return FALSE;
}

s32 act_double_jump(struct MarioState *m) {
    s32 animation = (m->vel[1] >= 0.0f)
        ? MARIO_ANIM_DOUBLE_JUMP_RISE
        : MARIO_ANIM_DOUBLE_JUMP_FALL;

    if (check_kick_or_dive_in_air(m)) {
        return TRUE;
    }

    if (m->input & INPUT_Z_PRESSED) {
        return set_mario_action(m, ACT_GROUND_POUND, 0);
    }

    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, SOUND_MARIO_HOOHOO);
    common_air_action_step(m, ACT_DOUBLE_JUMP_LAND, animation,
                           AIR_STEP_CHECK_LEDGE_GRAB | AIR_STEP_CHECK_HANG);
    return FALSE;
}

s32 act_triple_jump(struct MarioState *m) {
    if (gSpecialTripleJump) {
        return set_mario_action(m, ACT_SPECIAL_TRIPLE_JUMP, 0);
    }

    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, ACT_DIVE, 0);
    }

    if (m->input & INPUT_Z_PRESSED) {
        return set_mario_action(m, ACT_GROUND_POUND, 0);
    }

    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, 0);

    common_air_action_step(m, ACT_TRIPLE_JUMP_LAND, MARIO_ANIM_TRIPLE_JUMP, 0);
#if ENABLE_RUMBLE
    if (m->action == ACT_TRIPLE_JUMP_LAND) {
        queue_rumble_data(5, 40);
    }
#endif
    play_flip_sounds(m, 2, 8, 20);
    return FALSE;
}

s32 act_backflip(struct MarioState *m) {
    if (m->input & INPUT_Z_PRESSED) {
        return set_mario_action(m, ACT_GROUND_POUND, 0);
    }

    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, SOUND_MARIO_YAH_WAH_HOO);
    common_air_action_step(m, ACT_BACKFLIP_LAND, MARIO_ANIM_BACKFLIP, 0);
#if ENABLE_RUMBLE
    if (m->action == ACT_BACKFLIP_LAND) {
        queue_rumble_data(5, 40);
    }
#endif
    play_flip_sounds(m, 2, 3, 17);
    return FALSE;
}

s32 act_freefall(struct MarioState *m) {
    s32 animation = MARIO_ANIM_GENERAL_FALL;

    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, ACT_DIVE, 0);
    }

    if (m->input & INPUT_Z_PRESSED) {
        return set_mario_action(m, ACT_GROUND_POUND, 0);
    }

    switch (m->actionArg) {
        case ACT_ARG_FREEFALL_GENERAL:
            animation = MARIO_ANIM_GENERAL_FALL;
            break;
        case ACT_ARG_FREEFALL_FROM_SLIDE:
            animation = MARIO_ANIM_FALL_FROM_SLIDE;
            break;
        case ACT_ARG_FREEFALL_FROM_SLIDE_KICK:
            animation = MARIO_ANIM_FALL_FROM_SLIDE_KICK;
            break;
    }

    common_air_action_step(m, ACT_FREEFALL_LAND, animation, AIR_STEP_CHECK_LEDGE_GRAB);
    return FALSE;
}

s32 act_hold_jump(struct MarioState *m) {
    if (m->marioObj->oInteractStatus & INT_STATUS_MARIO_DROP_OBJECT) {
        return drop_and_set_mario_action(m, ACT_FREEFALL, 0);
    }

    if ((m->input & INPUT_B_PRESSED) && !(m->heldObj->oInteractionSubtype & INT_SUBTYPE_HOLDABLE_NPC)) {
        return set_mario_action(m, ACT_AIR_THROW, 0);
    }

    if (m->input & INPUT_Z_PRESSED) {
        return drop_and_set_mario_action(m, ACT_GROUND_POUND, 0);
    }

    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, 0);
    common_air_action_step(m, ACT_HOLD_JUMP_LAND, MARIO_ANIM_JUMP_WITH_LIGHT_OBJ,
                           AIR_STEP_CHECK_LEDGE_GRAB);
    return FALSE;
}

s32 act_hold_freefall(struct MarioState *m) {
    s32 animation;
    if (m->actionArg == 0) {
        animation = MARIO_ANIM_FALL_WITH_LIGHT_OBJ;
    } else {
        animation = MARIO_ANIM_FALL_FROM_SLIDING_WITH_LIGHT_OBJ;
    }

    if (m->marioObj->oInteractStatus & INT_STATUS_MARIO_DROP_OBJECT) {
        return drop_and_set_mario_action(m, ACT_FREEFALL, 0);
    }

    if ((m->input & INPUT_B_PRESSED) && !(m->heldObj->oInteractionSubtype & INT_SUBTYPE_HOLDABLE_NPC)) {
        return set_mario_action(m, ACT_AIR_THROW, 0);
    }

    if (m->input & INPUT_Z_PRESSED) {
        return drop_and_set_mario_action(m, ACT_GROUND_POUND, 0);
    }

    common_air_action_step(m, ACT_HOLD_FREEFALL_LAND, animation, AIR_STEP_CHECK_LEDGE_GRAB);
    return FALSE;
}

s32 act_side_flip(struct MarioState *m) {
    if (m->input & INPUT_B_PRESSED) {
        m->marioObj->header.gfx.angle[1] += 0x8000;
        return set_mario_action(m, ACT_DIVE, 0);
    }

    if (m->input & INPUT_Z_PRESSED) {
        m->marioObj->header.gfx.angle[1] += 0x8000;
        return set_mario_action(m, ACT_GROUND_POUND, 0);
    }

    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, 0);

    if (common_air_action_step(m, ACT_SIDE_FLIP_LAND, MARIO_ANIM_SLIDEFLIP, AIR_STEP_CHECK_LEDGE_GRAB)
        != AIR_STEP_GRABBED_LEDGE) {
        m->marioObj->header.gfx.angle[1] += 0x8000;
    }

    if (m->marioObj->header.gfx.animInfo.animFrame == 6) {
        play_sound(SOUND_ACTION_SIDE_FLIP_UNK, m->marioObj->header.gfx.cameraToObject);
    }
    return FALSE;
}

s32 act_wall_kick_air(struct MarioState *m) {
    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, ACT_DIVE, 0);
    }

    if (m->input & INPUT_Z_PRESSED) {
        return set_mario_action(m, ACT_GROUND_POUND, 0);
    }

    play_mario_jump_sound(m);
    common_air_action_step(m, ACT_JUMP_LAND, MARIO_ANIM_SLIDEJUMP, AIR_STEP_CHECK_LEDGE_GRAB);
    return FALSE;
}

s32 act_long_jump(struct MarioState *m) {
    s32 animation;
    if (!m->marioObj->oMarioLongJumpIsSlow) {
        animation = MARIO_ANIM_FAST_LONGJUMP;
    } else {
        animation = MARIO_ANIM_SLOW_LONGJUMP;
    }

    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, SOUND_MARIO_YAHOO);

#ifdef WIND_RESISTANT_METAL_CAP
    if (!(m->flags & MARIO_METAL_CAP) && SURFACE_IS_VERTICAL_WIND(m->floor->type) && m->actionState == 0) {
#else
    if (SURFACE_IS_VERTICAL_WIND(m->floor->type) && m->actionState == 0) {
#endif
        play_sound(SOUND_MARIO_HERE_WE_GO, m->marioObj->header.gfx.cameraToObject);
        m->actionState = 1;
    }

    common_air_action_step(m, ACT_LONG_JUMP_LAND, animation, AIR_STEP_CHECK_LEDGE_GRAB);
#if ENABLE_RUMBLE
    if (m->action == ACT_LONG_JUMP_LAND) {
        queue_rumble_data(5, 40);
    }
#endif
    return FALSE;
}

s32 act_riding_shell_air(struct MarioState *m) {
    set_mario_animation(m, MARIO_ANIM_JUMP_RIDING_SHELL);

#ifdef KOOPA_SHELL_COYOTE_TIME
    // if mario is falling start increment coyote timer (I remove jump mario sound when falling)
    if(m->action == ACT_RIDING_SHELL_FALL) 
        m->riddenObj->oCoyoteTimer++;
    else 
        play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, 0);
#else
    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, 0);
#endif

    update_air_without_turn(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_LANDED:
            set_mario_action(m, ACT_RIDING_SHELL_GROUND, 1);
            break;

        case AIR_STEP_HIT_WALL:
            mario_set_forward_vel(m, 0.0f);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    m->marioObj->header.gfx.pos[1] += 42.0f;
    return FALSE;
}

s32 act_twirling(struct MarioState *m) {
    s16 startTwirlYaw = m->twirlYaw;
    s16 yawVelTarget;

#ifdef Z_TWIRL
    if (m->input & INPUT_Z_DOWN) {
        yawVelTarget = 0x2800;
    } else
#endif

    if (m->input & INPUT_A_DOWN) {
        yawVelTarget = 0x2000;
    } else {
        yawVelTarget = 0x1800;
    }

    m->angleVel[1] = approach_s32_symmetric(m->angleVel[1], yawVelTarget, 0x200);
    m->twirlYaw += m->angleVel[1];

    set_mario_animation(m, m->actionArg == 0 ? MARIO_ANIM_START_TWIRL : MARIO_ANIM_TWIRL);
    if (is_anim_past_end(m)) {
        m->actionArg = 1;
    }

    if (startTwirlYaw > m->twirlYaw) {
        play_sound(SOUND_ACTION_TWIRL, m->marioObj->header.gfx.cameraToObject);
    }

    update_lava_boost_or_twirling(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_LANDED:
            set_mario_action(m, ACT_TWIRL_LAND, 0);
            break;

        case AIR_STEP_HIT_WALL:
            mario_bonk_reflection(m, FALSE);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    m->marioObj->header.gfx.angle[1] += m->twirlYaw;
#if ENABLE_RUMBLE
    reset_rumble_timers_slip();
#endif
    return FALSE;
}

s32 act_dive(struct MarioState *m) {
    if (m->actionArg == 0) {
        play_mario_sound(m, SOUND_ACTION_THROW, SOUND_MARIO_HOOHOO);
    } else {
        play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, 0);
    }

    set_mario_animation(m, MARIO_ANIM_DIVE);
    if (mario_check_object_grab(m)) {
        mario_grab_used_object(m);
        m->marioBodyState->grabPos = GRAB_POS_LIGHT_OBJ;
        if (m->action != ACT_DIVE) {
            return TRUE;
        }
    }

    update_air_without_turn(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_NONE:
            if (m->vel[1] < 0.0f && m->faceAngle[0] > -DEGREES(60)) {
                m->faceAngle[0] -= 0x200;
                if (m->faceAngle[0] < -DEGREES(60)) {
                    m->faceAngle[0] = -DEGREES(60);
                }
            }
            m->marioObj->header.gfx.angle[0] = -m->faceAngle[0];
            break;

        case AIR_STEP_LANDED:
            if (should_get_stuck_in_ground(m) && m->faceAngle[0] == -DEGREES(60)) {
#if ENABLE_RUMBLE
                queue_rumble_data(5, 80);
#endif
                play_sound(SOUND_MARIO_OOOF2, m->marioObj->header.gfx.cameraToObject);
                m->particleFlags |= PARTICLE_MIST_CIRCLE;
                drop_and_set_mario_action(m, ACT_HEAD_STUCK_IN_GROUND, 0);
            } else if (!check_fall_damage(m, ACT_HARD_FORWARD_GROUND_KB)) {
                if (m->heldObj == NULL) {
                    set_mario_action(m, ACT_DIVE_SLIDE, 0);
                } else {
                    set_mario_action(m, ACT_DIVE_PICKING_UP, 0);
                }
            }
            m->faceAngle[0] = 0;
            break;

        case AIR_STEP_HIT_WALL:
            mario_bonk_reflection(m, TRUE);
            m->faceAngle[0] = 0;

            if (m->vel[1] > 0.0f) {
                m->vel[1] = 0.0f;
            }

            m->particleFlags |= PARTICLE_VERTICAL_STAR;
            drop_and_set_mario_action(m, ACT_BACKWARD_AIR_KB, 0);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    return FALSE;
}

s32 act_air_throw(struct MarioState *m) {
    if (++(m->actionTimer) == 4) {
        mario_throw_held_object(m);
    }

    play_sound_if_no_flag(m, SOUND_MARIO_WAH2, MARIO_MARIO_SOUND_PLAYED);
    set_mario_animation(m, MARIO_ANIM_THROW_LIGHT_OBJECT);
    update_air_without_turn(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_LANDED:
            if (!check_fall_damage_or_get_stuck(m, ACT_HARD_BACKWARD_GROUND_KB)) {
                m->action = ACT_AIR_THROW_LAND;
            }
            break;

        case AIR_STEP_HIT_WALL:
            mario_set_forward_vel(m, 0.0f);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    return FALSE;
}

s32 act_water_jump(struct MarioState *m) {
    if (m->forwardVel < 15.0f) {
        mario_set_forward_vel(m, 15.0f);
    }

    play_mario_sound(m, SOUND_ACTION_WATER_JUMP, 0);
    set_mario_animation(m, MARIO_ANIM_SINGLE_JUMP);

    switch (perform_air_step(m, AIR_STEP_CHECK_LEDGE_GRAB)) {
        case AIR_STEP_LANDED:
            set_mario_action(m, ACT_JUMP_LAND, 0);
            set_camera_mode(m->area->camera, m->area->camera->defMode, 1);
            break;

        case AIR_STEP_HIT_WALL:
            mario_set_forward_vel(m, 15.0f);
            break;

        case AIR_STEP_GRABBED_LEDGE:
            set_mario_animation(m, MARIO_ANIM_IDLE_ON_LEDGE);
            set_mario_action(m, ACT_LEDGE_GRAB, 0);
            set_camera_mode(m->area->camera, m->area->camera->defMode, 1);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    return FALSE;
}

s32 act_hold_water_jump(struct MarioState *m) {
    if (m->marioObj->oInteractStatus & INT_STATUS_MARIO_DROP_OBJECT) {
        return drop_and_set_mario_action(m, ACT_FREEFALL, 0);
    }

    if (m->forwardVel < 15.0f) {
        mario_set_forward_vel(m, 15.0f);
    }

    play_mario_sound(m, SOUND_ACTION_WATER_JUMP, 0);
    set_mario_animation(m, MARIO_ANIM_JUMP_WITH_LIGHT_OBJ);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_LANDED:
            set_mario_action(m, ACT_HOLD_JUMP_LAND, 0);
            set_camera_mode(m->area->camera, m->area->camera->defMode, 1);
            break;

        case AIR_STEP_HIT_WALL:
            mario_set_forward_vel(m, 15.0f);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    return FALSE;
}

s32 act_steep_jump(struct MarioState *m) {
    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, ACT_DIVE, 0);
    }

    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, 0);
    mario_set_forward_vel(m, 0.98f * m->forwardVel);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_LANDED:
            if (!check_fall_damage_or_get_stuck(m, ACT_HARD_BACKWARD_GROUND_KB)) {
                m->faceAngle[0] = 0;
                set_mario_action(m, m->forwardVel < 0.0f ? ACT_BEGIN_SLIDING : ACT_JUMP_LAND, 0);
            }
            break;

        case AIR_STEP_HIT_WALL:
            mario_set_forward_vel(m, 0.0f);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    set_mario_animation(m, MARIO_ANIM_SINGLE_JUMP);
    m->marioObj->header.gfx.angle[1] = m->marioObj->oMarioSteepJumpYaw;
    return FALSE;
}

s32 act_ground_pound(struct MarioState *m) {
    u32 stepResult;
    f32 yOffset;

    play_sound_if_no_flag(m, SOUND_ACTION_THROW, MARIO_ACTION_SOUND_PLAYED);

    if (m->actionState == 0) {
        if (m->actionTimer < 10) {
            yOffset = 20 - 2 * m->actionTimer;
            if (m->pos[1] + yOffset + 160.0f < m->ceilHeight) {
                m->pos[1] += yOffset;
                m->peakHeight = m->pos[1];
                vec3f_copy(m->marioObj->header.gfx.pos, m->pos);
            }
        }

        m->vel[1] = -50.0f;
        mario_set_forward_vel(m, 0.0f);

        set_mario_animation(m, m->actionArg == ACT_ARG_GROUND_POUND_NORMAL ? MARIO_ANIM_START_GROUND_POUND
                                                                           : MARIO_ANIM_TRIPLE_JUMP_GROUND_POUND);
        if (m->actionTimer == 0) {
            play_sound(SOUND_ACTION_SPIN, m->marioObj->header.gfx.cameraToObject);
        }

        m->actionTimer++;
        if (m->actionTimer >= m->marioObj->header.gfx.animInfo.curAnim->loopEnd + 4) {
            play_sound(SOUND_MARIO_GROUND_POUND_WAH, m->marioObj->header.gfx.cameraToObject);
            m->actionState = ACT_STATE_GROUND_POUND_FALL;
        }
    } else {
        set_mario_animation(m, MARIO_ANIM_GROUND_POUND);

        stepResult = perform_air_step(m, 0);
        if (stepResult == AIR_STEP_LANDED) {
            if (should_get_stuck_in_ground(m)) {
#if ENABLE_RUMBLE
                queue_rumble_data(5, 80);
#endif
                play_sound(SOUND_MARIO_OOOF2, m->marioObj->header.gfx.cameraToObject);
                m->particleFlags |= PARTICLE_MIST_CIRCLE;
                set_mario_action(m, ACT_BUTT_STUCK_IN_GROUND, 0);
            } else {
                play_mario_heavy_landing_sound(m, SOUND_ACTION_TERRAIN_HEAVY_LANDING);
                if (!check_fall_damage(m, ACT_HARD_BACKWARD_GROUND_KB)) {
                    m->particleFlags |= PARTICLE_MIST_CIRCLE | PARTICLE_HORIZONTAL_STAR;
                    set_mario_action(m, ACT_GROUND_POUND_LAND, 0);
                }
            }
            set_camera_shake_from_hit(SHAKE_GROUND_POUND);
        }
#ifndef DISABLE_GROUNDPOUND_BONK
        else if (stepResult == AIR_STEP_HIT_WALL) {
            mario_set_forward_vel(m, -16.0f);
            if (m->vel[1] > 0.0f) {
                m->vel[1] = 0.0f;
            }

            m->particleFlags |= PARTICLE_VERTICAL_STAR;
            set_mario_action(m, ACT_BACKWARD_AIR_KB, 0);
        }
#endif
    }

    return FALSE;
}

s32 act_burning_jump(struct MarioState *m) {
    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, m->actionArg == 0 ? 0 : -1);
    mario_set_forward_vel(m, m->forwardVel);

    if (perform_air_step(m, 0) == AIR_STEP_LANDED) {
        play_mario_landing_sound(m, SOUND_ACTION_TERRAIN_LANDING);
        set_mario_action(m, ACT_BURNING_GROUND, 0);
    }

    set_mario_animation(m, m->actionArg == 0 ? MARIO_ANIM_SINGLE_JUMP : MARIO_ANIM_FIRE_LAVA_BURN);
    m->particleFlags |= PARTICLE_FIRE;
    play_sound(SOUND_MOVING_LAVA_BURN, m->marioObj->header.gfx.cameraToObject);

    m->marioObj->oMarioBurnTimer += 3;

    m->health -= 10;
    if (m->health < 0x100) {
        m->health = 0xFF;
    }
#if ENABLE_RUMBLE
    reset_rumble_timers_slip();
#endif
    return FALSE;
}

s32 act_burning_fall(struct MarioState *m) {
    mario_set_forward_vel(m, m->forwardVel);

    if (perform_air_step(m, 0) == AIR_STEP_LANDED) {
        play_mario_landing_sound(m, SOUND_ACTION_TERRAIN_LANDING);
        set_mario_action(m, ACT_BURNING_GROUND, 0);
    }

    set_mario_animation(m, MARIO_ANIM_GENERAL_FALL);
    m->particleFlags |= PARTICLE_FIRE;
    m->marioObj->oMarioBurnTimer += 3;

    m->health -= 10;
    if (m->health < 0x100) {
        m->health = 0xFF;
    }
#if ENABLE_RUMBLE
    reset_rumble_timers_slip();
#endif
    return FALSE;
}

s32 act_crazy_box_bounce(struct MarioState *m) {
    f32 minSpeed = 0.0f;

    if (m->actionTimer == 0) {
        switch (m->actionArg) {
            case 0:
                m->vel[1] = 45.0f;
                minSpeed = 32.0f;
                break;

            case 1:
                m->vel[1] = 60.0f;
                minSpeed = 36.0f;
                break;

            case 2:
                m->vel[1] = 100.0f;
                minSpeed = 48.0f;
                break;
        }

        play_sound(minSpeed < 40.0f ? SOUND_GENERAL_CRAZY_BOX_BOING_SLOW : SOUND_GENERAL_CRAZY_BOX_BOING_FAST,
                   m->marioObj->header.gfx.cameraToObject);

        if (m->forwardVel < minSpeed) {
            mario_set_forward_vel(m, minSpeed);
        }

        m->actionTimer = 1;
    }

    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, 0);
    set_mario_animation(m, MARIO_ANIM_DIVE);

    update_air_without_turn(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_LANDED:
            if (m->actionArg < 2) {
                set_mario_action(m, ACT_CRAZY_BOX_BOUNCE, m->actionArg + 1);
            } else {
                m->heldObj->oInteractStatus = INT_STATUS_STOP_RIDING;
                m->heldObj = NULL;
                set_mario_action(m, ACT_STOMACH_SLIDE, 0);
            }
#if ENABLE_RUMBLE
            queue_rumble_data(5, 80);
#endif
            m->particleFlags |= PARTICLE_MIST_CIRCLE;
            break;

        case AIR_STEP_HIT_WALL:
            mario_bonk_reflection(m, FALSE);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    m->marioObj->header.gfx.angle[0] = atan2s(m->forwardVel, -m->vel[1]);
    return FALSE;
}

u32 common_air_knockback_step(struct MarioState *m, u32 landAction, u32 hardFallAction, s32 animation,
                              f32 speed) {
    u32 stepResult;

    mario_set_forward_vel(m, speed);

    stepResult = perform_air_step(m, 0);
    switch (stepResult) {
        case AIR_STEP_NONE:
            set_mario_animation(m, animation);
            break;

        case AIR_STEP_LANDED:
#if ENABLE_RUMBLE
            if (m->action != ACT_SOFT_BONK) {
                queue_rumble_data(5, 40);
            }
#endif
            if (!check_fall_damage_or_get_stuck(m, hardFallAction)) {
                if (m->action == ACT_THROWN_FORWARD || m->action == ACT_THROWN_BACKWARD) {
                    set_mario_action(m, landAction, m->hurtCounter);
                } else {
                    set_mario_action(m, landAction, m->actionArg);
                }
            }
            break;

        case AIR_STEP_HIT_WALL:
            set_mario_animation(m, MARIO_ANIM_BACKWARD_AIR_KB);
            mario_bonk_reflection(m, FALSE);

            if (m->vel[1] > 0.0f) {
                m->vel[1] = 0.0f;
            }

            mario_set_forward_vel(m, -speed);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    return stepResult;
}

s32 check_wall_kick(struct MarioState *m) {
    if ((m->input & INPUT_A_PRESSED) && m->wallKickTimer != 0 && m->prevAction == ACT_AIR_HIT_WALL) {
        m->faceAngle[1] += 0x8000;
        return set_mario_action(m, ACT_WALL_KICK_AIR, 0);
    }

    return FALSE;
}

s32 act_backward_air_kb(struct MarioState *m) {
    if (check_wall_kick(m)) {
        return TRUE;
    }

    play_knockback_sound(m);
    common_air_knockback_step(m, ACT_BACKWARD_GROUND_KB, ACT_HARD_BACKWARD_GROUND_KB, MARIO_ANIM_BACKWARD_AIR_KB, -16.0f);
    return FALSE;
}

s32 act_forward_air_kb(struct MarioState *m) {
    if (check_wall_kick(m)) {
        return TRUE;
    }

    play_knockback_sound(m);
    common_air_knockback_step(m, ACT_FORWARD_GROUND_KB, ACT_HARD_FORWARD_GROUND_KB, MARIO_ANIM_AIR_FORWARD_KB, 16.0f);
    return FALSE;
}

s32 act_hard_backward_air_kb(struct MarioState *m) {
    play_knockback_sound(m);
    common_air_knockback_step(m, ACT_HARD_BACKWARD_GROUND_KB, ACT_HARD_BACKWARD_GROUND_KB, MARIO_ANIM_BACKWARD_AIR_KB, -16.0f);
    return FALSE;
}

s32 act_hard_forward_air_kb(struct MarioState *m) {
    play_knockback_sound(m);
    common_air_knockback_step(m, ACT_HARD_FORWARD_GROUND_KB, ACT_HARD_FORWARD_GROUND_KB, MARIO_ANIM_AIR_FORWARD_KB, 16.0f);
    return FALSE;
}

s32 act_thrown_backward(struct MarioState *m) {
    u32 landAction;
    if (m->actionArg != 0) {
        landAction = ACT_HARD_BACKWARD_GROUND_KB;
    } else {
        landAction = ACT_BACKWARD_GROUND_KB;
    }

    play_sound_if_no_flag(m, SOUND_MARIO_WAAAOOOW, MARIO_MARIO_SOUND_PLAYED);

    common_air_knockback_step(m, landAction, ACT_HARD_BACKWARD_GROUND_KB, MARIO_ANIM_BACKWARD_AIR_KB, m->forwardVel);

    m->forwardVel *= 0.98f;
    return FALSE;
}

s32 act_thrown_forward(struct MarioState *m) {
    s16 pitch;

    u32 landAction;
    if (m->actionArg != 0) {
        landAction = ACT_HARD_FORWARD_GROUND_KB;
    } else {
        landAction = ACT_FORWARD_GROUND_KB;
    }

    play_sound_if_no_flag(m, SOUND_MARIO_WAAAOOOW, MARIO_MARIO_SOUND_PLAYED);

    if (common_air_knockback_step(m, landAction, ACT_HARD_FORWARD_GROUND_KB, MARIO_ANIM_AIR_FORWARD_KB, m->forwardVel) == AIR_STEP_NONE) {
        pitch = atan2s(m->forwardVel, -m->vel[1]);
        if (pitch > 0x1800) {
            pitch = 0x1800;
        }

        m->marioObj->header.gfx.angle[0] = pitch + 0x1800;
    }

    m->forwardVel *= 0.98f;
    return FALSE;
}

s32 act_soft_bonk(struct MarioState *m) {
    if (check_wall_kick(m)) {
        return TRUE;
    }

    play_knockback_sound(m);

    common_air_knockback_step(m, ACT_FREEFALL_LAND, ACT_HARD_BACKWARD_GROUND_KB, MARIO_ANIM_GENERAL_FALL, m->forwardVel);
    return FALSE;
}

s32 act_getting_blown(struct MarioState *m) {
    if (m->actionState == ACT_STATE_GETTING_BLOWN_ACCEL_BACKWARDS) {
        if (m->forwardVel > -60.0f) {
            m->forwardVel -= 6.0f;
        } else {
            m->actionState = ACT_STATE_GETTING_BLOWN_SLOW_DOWN;
        }
    } else {
        if (m->forwardVel < -16.0f) {
            m->forwardVel += 0.8f;
        }

        if (m->vel[1] < 0.0f && m->windGravity < 4.0f) {
            m->windGravity += 0.05f;
        }
    }
#ifndef PREVENT_CAP_LOSS
    if (++(m->actionTimer) == 20) {
        mario_blow_off_cap(m, 50.0f);
    }
#endif

    mario_set_forward_vel(m, m->forwardVel);
    set_mario_animation(m, MARIO_ANIM_BACKWARD_AIR_KB);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_LANDED:
            set_mario_action(m, ACT_HARD_BACKWARD_AIR_KB, 0);
            break;

        case AIR_STEP_HIT_WALL:
            set_mario_animation(m, MARIO_ANIM_AIR_FORWARD_KB);
            mario_bonk_reflection(m, FALSE);

            if (m->vel[1] > 0.0f) {
                m->vel[1] = 0.0f;
            }

            mario_set_forward_vel(m, -m->forwardVel);
            break;
    }

    return FALSE;
}

s32 act_air_hit_wall(struct MarioState *m) {
    if (m->heldObj != NULL) {
        mario_drop_held_object(m);
    }

    if (++(m->actionTimer) <= 2) {
        if (m->input & INPUT_A_PRESSED) {
            m->vel[1] = 52.0f;
            m->faceAngle[1] += 0x8000;
            return set_mario_action(m, ACT_WALL_KICK_AIR, 0);
        }
    } else if (m->forwardVel >= 38.0f) {
        m->wallKickTimer = 5;
        if (m->vel[1] > 0.0f) {
            m->vel[1] = 0.0f;
        }

        m->particleFlags |= PARTICLE_VERTICAL_STAR;
        return set_mario_action(m, ACT_BACKWARD_AIR_KB, 0);
    } else {
        m->wallKickTimer = 5;
        if (m->vel[1] > 0.0f) {
            m->vel[1] = 0.0f;
        }

        if (m->forwardVel > 8.0f) {
            mario_set_forward_vel(m, -8.0f);
        }
        return set_mario_action(m, ACT_SOFT_BONK, 0);
    }

    set_mario_animation(m, MARIO_ANIM_START_WALLKICK);

    return TRUE;
}

s32 act_forward_rollout(struct MarioState *m) {
    if (m->actionState == 0) {
        m->vel[1] = 30.0f;
        m->actionState = 1;
    }

    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, 0);

    update_air_without_turn(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_NONE:
            if (m->actionState == 1) {
                if (set_mario_animation(m, MARIO_ANIM_FORWARD_SPINNING) == 4) {
                    play_sound(SOUND_ACTION_SPIN, m->marioObj->header.gfx.cameraToObject);
                }
            } else {
                set_mario_animation(m, MARIO_ANIM_GENERAL_FALL);
            }
            break;

        case AIR_STEP_LANDED:
            set_mario_action(m, ACT_FREEFALL_LAND_STOP, 0);
            play_mario_landing_sound(m, SOUND_ACTION_TERRAIN_LANDING);
            break;

        case AIR_STEP_HIT_WALL:
            mario_set_forward_vel(m, 0.0f);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    if (m->actionState == 1 && is_anim_past_end(m)) {
        m->actionState = 2;
    }
    return FALSE;
}

s32 act_backward_rollout(struct MarioState *m) {
    if (m->actionState == 0) {
        m->vel[1] = 30.0f;
        m->actionState = 1;
    }

    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, 0);

    update_air_without_turn(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_NONE:
            if (m->actionState == 1) {
                if (set_mario_animation(m, MARIO_ANIM_BACKWARD_SPINNING) == 4) {
                    play_sound(SOUND_ACTION_SPIN, m->marioObj->header.gfx.cameraToObject);
                }
            } else {
                set_mario_animation(m, MARIO_ANIM_GENERAL_FALL);
            }
            break;

        case AIR_STEP_LANDED:
            set_mario_action(m, ACT_FREEFALL_LAND_STOP, 0);
            play_mario_landing_sound(m, SOUND_ACTION_TERRAIN_LANDING);
            break;

        case AIR_STEP_HIT_WALL:
            mario_set_forward_vel(m, 0.0f);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    if (m->actionState == 1 && m->marioObj->header.gfx.animInfo.animFrame == 2) {
        m->actionState = 2;
    }
    return FALSE;
}

s32 act_butt_slide_air(struct MarioState *m) {
    if (++(m->actionTimer) > 30 && m->pos[1] - m->floorHeight > 500.0f) {
        return set_mario_action(m, ACT_FREEFALL, 1);
    }

    update_air_with_turn(m);

    switch (perform_air_step(m, AIR_STEP_CHECK_NONE)) {
        case AIR_STEP_LANDED:
            if (m->actionState == 0 && m->vel[1] < 0.0f && m->floor->normal.y >= COS10) {
                m->vel[1] = -m->vel[1] / 2.0f;
                m->actionState = 1;
            } else {
                set_mario_action(m, ACT_BUTT_SLIDE, 0);
            }
            play_mario_landing_sound(m, SOUND_ACTION_TERRAIN_LANDING);
            break;

        case AIR_STEP_HIT_WALL:
            if (m->vel[1] > 0.0f) {
                m->vel[1] = 0.0f;
            }
            m->particleFlags |= PARTICLE_VERTICAL_STAR;
            set_mario_action(m, ACT_BACKWARD_AIR_KB, 0);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    set_mario_animation(m, MARIO_ANIM_SLIDE);
    return FALSE;
}

s32 act_hold_butt_slide_air(struct MarioState *m) {
    if (m->marioObj->oInteractStatus & INT_STATUS_MARIO_DROP_OBJECT) {
        return drop_and_set_mario_action(m, ACT_HOLD_FREEFALL, 1);
    }

    if (++m->actionTimer > 30 && m->pos[1] - m->floorHeight > 500.0f) {
        return set_mario_action(m, ACT_HOLD_FREEFALL, 1);
    }

    update_air_with_turn(m);

    switch (perform_air_step(m, AIR_STEP_CHECK_NONE)) {
        case AIR_STEP_LANDED:
            if (m->actionState == ACT_STATE_BUTT_SLIDE_AIR_SMALL_BOUNCE && m->vel[1] < 0.0f && m->floor->normal.y >= COS10) {
                m->vel[1] = -m->vel[1] / 2.0f;
                m->actionState = 1;
            } else {
                set_mario_action(m, ACT_HOLD_BUTT_SLIDE, 0);
            }
            play_mario_landing_sound(m, SOUND_ACTION_TERRAIN_LANDING);
            break;

        case AIR_STEP_HIT_WALL:
            if (m->vel[1] > 0.0f) {
                m->vel[1] = 0.0f;
            }

            mario_drop_held_object(m);
            m->particleFlags |= PARTICLE_VERTICAL_STAR;
            set_mario_action(m, ACT_BACKWARD_AIR_KB, 0);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    set_mario_animation(m, MARIO_ANIM_SLIDING_ON_BOTTOM_WITH_LIGHT_OBJ);
    return FALSE;
}

s32 act_lava_boost(struct MarioState *m) {
#if ENABLE_RUMBLE
    if (!(m->flags & MARIO_MARIO_SOUND_PLAYED)) {
#endif
        play_sound_if_no_flag(m, SOUND_MARIO_ON_FIRE, MARIO_MARIO_SOUND_PLAYED);
#if ENABLE_RUMBLE
        queue_rumble_data(5, 80);
    }
#endif

    if (!(m->input & INPUT_NONZERO_ANALOG)) {
        m->forwardVel = approach_f32(m->forwardVel, 0.0f, 0.35f, 0.35f);
    }

    update_lava_boost_or_twirling(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_LANDED:
            if (m->floor->type == SURFACE_BURNING) {
                m->actionState = ACT_STATE_LAVA_BOOST_HIT_LAVA;
                if (!(m->flags & MARIO_METAL_CAP)) {
                    m->hurtCounter += (m->flags & MARIO_CAP_ON_HEAD) ? 12 : 18;
                }
                m->vel[1] = 84.0f;
                play_sound(SOUND_MARIO_ON_FIRE, m->marioObj->header.gfx.cameraToObject);
#if ENABLE_RUMBLE
                queue_rumble_data(5, 80);
#endif
            } else {
                play_mario_heavy_landing_sound(m, SOUND_ACTION_TERRAIN_BODY_HIT_GROUND);
                if (m->actionState < ACT_STATE_LAVA_BOOST_SET_LANDING_ACTION && m->vel[1] < 0.0f) {
                    m->vel[1] = -m->vel[1] * 0.4f;
                    mario_set_forward_vel(m, m->forwardVel * 0.5f);
                    m->actionState++;
                } else {
                    set_mario_action(m, ACT_LAVA_BOOST_LAND, 0);
                }
            }
            break;

        case AIR_STEP_HIT_WALL:
            mario_bonk_reflection(m, FALSE);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    set_mario_animation(m, MARIO_ANIM_FIRE_LAVA_BURN);
    if ((m->area->terrainType & TERRAIN_MASK) != TERRAIN_SNOW && !(m->flags & MARIO_METAL_CAP)
        && m->vel[1] > 0.0f) {
        m->particleFlags |= PARTICLE_FIRE;
        if (m->actionState == ACT_STATE_LAVA_BOOST_HIT_LAVA) {
            play_sound(SOUND_MOVING_LAVA_BURN, m->marioObj->header.gfx.cameraToObject);
        }
    }

    if (m->health < 0x100) {
        level_trigger_warp(m, WARP_OP_DEATH);
    }

    m->marioBodyState->eyeState = MARIO_EYES_DEAD;
#if ENABLE_RUMBLE
    reset_rumble_timers_slip();
#endif
    return FALSE;
}

s32 act_slide_kick(struct MarioState *m) {
    if (m->actionState == ACT_STATE_SLIDE_KICK_SLIDING && m->actionTimer == 0) {
        play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, SOUND_MARIO_HOOHOO);
        set_mario_animation(m, MARIO_ANIM_SLIDE_KICK);
    }

    if (++(m->actionTimer) > 30 && m->pos[1] - m->floorHeight > 500.0f) {
        return set_mario_action(m, ACT_FREEFALL, 2);
    }

    update_air_without_turn(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_NONE:
            if (m->actionState == ACT_STATE_SLIDE_KICK_SLIDING) {
                m->marioObj->header.gfx.angle[0] = atan2s(m->forwardVel, -m->vel[1]);
                if (m->marioObj->header.gfx.angle[0] > 0x1800) {
                    m->marioObj->header.gfx.angle[0] = 0x1800;
                }
            }
            break;

        case AIR_STEP_LANDED:
            if (m->actionState == ACT_STATE_SLIDE_KICK_SLIDING && m->vel[1] < 0.0f) {
                m->vel[1] = -m->vel[1] / 2.0f;
                m->actionState = ACT_STATE_SLIDE_KICK_END;
                m->actionTimer = 0;
            } else {
                set_mario_action(m, ACT_SLIDE_KICK_SLIDE, 0);
            }
            play_mario_landing_sound(m, SOUND_ACTION_TERRAIN_LANDING);
            break;

        case AIR_STEP_HIT_WALL:
            if (m->vel[1] > 0.0f) {
                m->vel[1] = 0.0f;
            }

            m->particleFlags |= PARTICLE_VERTICAL_STAR;

            set_mario_action(m, ACT_BACKWARD_AIR_KB, 0);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    return FALSE;
}

s32 act_jump_kick(struct MarioState *m) {
    if (m->actionState == ACT_STATE_JUMP_KICK_PLAY_SOUND_AND_ANIM) {
        play_sound_if_no_flag(m, SOUND_MARIO_PUNCH_HOO, MARIO_ACTION_SOUND_PLAYED);
        m->marioObj->header.gfx.animInfo.animID = -1;
        set_mario_animation(m, MARIO_ANIM_AIR_KICK);
        m->actionState = ACT_STATE_JUMP_KICK_KICKING;
    }

    s32 animFrame = m->marioObj->header.gfx.animInfo.animFrame;
    if (animFrame == 0) {
        m->marioBodyState->punchState = (PUNCH_STATE_TYPE_KICK | 0x6);
    }
    if (animFrame >= 0 && animFrame < 8) {
        m->flags |= MARIO_KICKING;
    }

    update_air_without_turn(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_LANDED:
            if (!check_fall_damage_or_get_stuck(m, ACT_HARD_BACKWARD_GROUND_KB)) {
                set_mario_action(m, ACT_FREEFALL_LAND, 0);
            }
            break;

        case AIR_STEP_HIT_WALL:
            mario_set_forward_vel(m, 0.0f);
            break;
    }

    return FALSE;
}

s32 act_shot_from_cannon(struct MarioState *m) {
    if (m->area->camera->mode != CAMERA_MODE_BEHIND_MARIO) {
        m->statusForCamera->cameraEvent = CAM_EVENT_SHOT_FROM_CANNON;
    }

    mario_set_forward_vel(m, m->forwardVel);

    play_sound_if_no_flag(m, SOUND_MARIO_YAHOO, MARIO_MARIO_SOUND_PLAYED);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_NONE:
            set_mario_animation(m, MARIO_ANIM_AIRBORNE_ON_STOMACH);
            m->faceAngle[0] = atan2s(m->forwardVel, m->vel[1]);
            m->marioObj->header.gfx.angle[0] = -m->faceAngle[0];
            break;

        case AIR_STEP_LANDED:
            set_mario_action(m, ACT_DIVE_SLIDE, 0);
            m->faceAngle[0] = 0;
            set_camera_mode(m->area->camera, m->area->camera->defMode, 1);
#if ENABLE_RUMBLE
            queue_rumble_data(5, 80);
#endif
            break;

        case AIR_STEP_HIT_WALL:
            mario_set_forward_vel(m, -16.0f);

            m->faceAngle[0] = 0;
            if (m->vel[1] > 0.0f) {
                m->vel[1] = 0.0f;
            }

            m->particleFlags |= PARTICLE_VERTICAL_STAR;
            set_mario_action(m, ACT_BACKWARD_AIR_KB, 0);
            set_camera_mode(m->area->camera, m->area->camera->defMode, 1);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    if ((m->flags & MARIO_WING_CAP) && m->vel[1] < 0.0f) {
        set_mario_action(m, ACT_FLYING, 0);
    }

    if ((m->forwardVel -= 0.05f) < 10.0f) {
        mario_set_forward_vel(m, 10.0f);
    }

    if (m->vel[1] > 0.0f) {
        m->particleFlags |= PARTICLE_DUST;
    }
#if ENABLE_RUMBLE
    reset_rumble_timers_slip();
#endif
    return FALSE;
}

s32 act_flying(struct MarioState *m) {
    s16 startPitch = m->faceAngle[0];

    if (m->input & INPUT_Z_PRESSED) {
        if (m->area->camera->mode == FLYING_CAMERA_MODE) {
            set_camera_mode(m->area->camera, m->area->camera->defMode, 1);
        }
        return set_mario_action(m, ACT_GROUND_POUND, 1);
    }

    if (!(m->flags & MARIO_WING_CAP)) {
        if (m->area->camera->mode == FLYING_CAMERA_MODE) {
            set_camera_mode(m->area->camera, m->area->camera->defMode, 1);
        }
        return set_mario_action(m, ACT_FREEFALL, 0);
    }

    if (m->area->camera->mode != FLYING_CAMERA_MODE) {
        set_camera_mode(m->area->camera, FLYING_CAMERA_MODE, 1);
    }

    if (m->actionState == ACT_STATE_FLYING_SPIN) {
        if (m->actionArg == ACT_ARG_FLYING_FROM_CANNON) {
            set_mario_animation(m, MARIO_ANIM_FLY_FROM_CANNON);
        } else {
            set_mario_animation(m, MARIO_ANIM_FORWARD_SPINNING_FLIP);
            if (m->marioObj->header.gfx.animInfo.animFrame == 1) {
                play_sound(SOUND_ACTION_SPIN, m->marioObj->header.gfx.cameraToObject);
            }
        }

        if (is_anim_at_end(m)) {
            if (m->actionArg == ACT_ARG_FLYING_TOTWC) {
                load_level_init_text(0);
                m->actionArg = ACT_ARG_FLYING_DEFAULT;
            }

            set_mario_animation(m, MARIO_ANIM_WING_CAP_FLY);
            m->actionState = ACT_STATE_FLYING_FLY;
        }
    }

    update_flying(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_NONE:
            m->marioObj->header.gfx.angle[0] = -m->faceAngle[0];
            m->marioObj->header.gfx.angle[2] = m->faceAngle[2];
            m->actionTimer = 0;
            break;

        case AIR_STEP_LANDED:
            set_mario_action(m, ACT_DIVE_SLIDE, 0);

            set_mario_animation(m, MARIO_ANIM_DIVE);
            set_anim_to_frame(m, 7);

            m->faceAngle[0] = 0;
            set_camera_mode(m->area->camera, m->area->camera->defMode, 1);
#if ENABLE_RUMBLE
            queue_rumble_data(5, 60);
#endif
            break;

        case AIR_STEP_HIT_WALL:
            if (m->wall != NULL) {
                mario_set_forward_vel(m, -16.0f);
                m->faceAngle[0] = 0;

                if (m->vel[1] > 0.0f) {
                    m->vel[1] = 0.0f;
                }

                play_sound((m->flags & MARIO_METAL_CAP) ? SOUND_ACTION_METAL_BONK
                                                        : SOUND_ACTION_BONK,
                           m->marioObj->header.gfx.cameraToObject);

                m->particleFlags |= PARTICLE_VERTICAL_STAR;
                set_mario_action(m, ACT_BACKWARD_AIR_KB, 0);
                set_camera_mode(m->area->camera, m->area->camera->defMode, 1);
            } else {
                if (m->actionTimer++ == 0) {
                    play_sound(SOUND_ACTION_HIT, m->marioObj->header.gfx.cameraToObject);
                }

                if (m->actionTimer == 30) {
                    m->actionTimer = 0;
                }

                m->faceAngle[0] -= 0x200;
                if (m->faceAngle[0] < -DEGREES(60)) {
                    m->faceAngle[0] = -DEGREES(60);
                }

                m->marioObj->header.gfx.angle[0] = -m->faceAngle[0];
                m->marioObj->header.gfx.angle[2] = m->faceAngle[2];
            }
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    if (m->faceAngle[0] > 0x800 && m->forwardVel >= 48.0f) {
        m->particleFlags |= PARTICLE_DUST;
    }

    if (startPitch <= 0 && m->faceAngle[0] > 0 && m->forwardVel >= 48.0f) {
        play_sound(SOUND_ACTION_FLYING_FAST, m->marioObj->header.gfx.cameraToObject);
        play_sound(SOUND_MARIO_YAHOO_WAHA_YIPPEE + ((gAudioRandom % 5) << 16),
                   m->marioObj->header.gfx.cameraToObject);
#if ENABLE_RUMBLE
        queue_rumble_data(50, 40);
#endif
    }

    play_sound(SOUND_MOVING_FLYING, m->marioObj->header.gfx.cameraToObject);
    adjust_sound_for_speed(m);
    return FALSE;
}

s32 act_riding_hoot(struct MarioState *m) {
    if (!(m->input & INPUT_A_DOWN) || (m->marioObj->oInteractStatus & INT_STATUS_MARIO_DROP_FROM_HOOT)) {
        m->usedObj->oInteractStatus = 0;
        m->usedObj->oHootMarioReleaseTime = gGlobalTimer;

        play_sound_if_no_flag(m, SOUND_MARIO_UH, MARIO_MARIO_SOUND_PLAYED);
#if ENABLE_RUMBLE
        queue_rumble_data(4, 40);
#endif
        return set_mario_action(m, ACT_FREEFALL, 0);
    }

    m->pos[0] = m->usedObj->oPosX;
    m->pos[1] = m->usedObj->oPosY - 92.5f;
    m->pos[2] = m->usedObj->oPosZ;

    m->faceAngle[1] = 0x4000 - m->usedObj->oMoveAngleYaw;

    if (m->actionState == ACT_STATE_RIDING_HOOT_GRABBING) {
        set_mario_animation(m, MARIO_ANIM_HANG_ON_CEILING);
        if (is_anim_at_end(m)) {
            set_mario_animation(m, MARIO_ANIM_HANG_ON_OWL);
            m->actionState = ACT_STATE_RIDING_HOOT_HANGING;
        }
    }

    vec3f_set(m->vel, 0.0f, 0.0f, 0.0f);
    vec3f_set(m->marioObj->header.gfx.pos, m->pos[0], m->pos[1], m->pos[2]);
    vec3s_set(m->marioObj->header.gfx.angle, 0, 0x4000 - m->faceAngle[1], 0);
    return FALSE;
}

s32 act_flying_triple_jump(struct MarioState *m) {
    if (m->input & (INPUT_B_PRESSED | INPUT_Z_PRESSED)) {
        if (m->area->camera->mode == FLYING_CAMERA_MODE) {
            set_camera_mode(m->area->camera, m->area->camera->defMode, 1);
        }
        if (m->input & INPUT_B_PRESSED) {
            return set_mario_action(m, ACT_DIVE, 0);
        } else {
            return set_mario_action(m, ACT_GROUND_POUND, 0);
        }
    }

    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, SOUND_MARIO_YAHOO);
    if (m->actionState == ACT_STATE_FLYING_TRIPLE_JUMP_START) {
        set_mario_animation(m, MARIO_ANIM_TRIPLE_JUMP_FLY);

        if (m->marioObj->header.gfx.animInfo.animFrame == 7) {
            play_sound(SOUND_ACTION_SPIN, m->marioObj->header.gfx.cameraToObject);
        }

        if (is_anim_past_end(m)) {
            set_mario_animation(m, MARIO_ANIM_FORWARD_SPINNING);
#if ENABLE_RUMBLE
            queue_rumble_data(8, 80);
#endif
            m->actionState = ACT_STATE_FLYING_TRIPLE_JUMP_SPIN;
        }
    }

    if (m->actionState == ACT_STATE_FLYING_TRIPLE_JUMP_SPIN && m->marioObj->header.gfx.animInfo.animFrame == 1) {
        play_sound(SOUND_ACTION_SPIN, m->marioObj->header.gfx.cameraToObject);
    }

    if (m->vel[1] < 4.0f) {
        if (m->area->camera->mode != FLYING_CAMERA_MODE) {
            set_camera_mode(m->area->camera, FLYING_CAMERA_MODE, 1);
        }

        if (m->forwardVel < 32.0f) {
            mario_set_forward_vel(m, 32.0f);
        }

        set_mario_action(m, ACT_FLYING, 1);
    }

    if (m->actionTimer++ == 10 && m->area->camera->mode != FLYING_CAMERA_MODE) {
        set_camera_mode(m->area->camera, FLYING_CAMERA_MODE, 1);
    }

    update_air_without_turn(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_LANDED:
            if (!check_fall_damage_or_get_stuck(m, ACT_HARD_BACKWARD_GROUND_KB)) {
                set_mario_action(m, ACT_DOUBLE_JUMP_LAND, 0);
            }
            break;

        case AIR_STEP_HIT_WALL:
            mario_bonk_reflection(m, FALSE);
            break;

        case AIR_STEP_HIT_LAVA_WALL:
            lava_boost_on_wall(m);
            break;
    }

    return FALSE;
}

s32 act_top_of_pole_jump(struct MarioState *m) {
    play_mario_jump_sound(m);
    common_air_action_step(m, ACT_FREEFALL_LAND, MARIO_ANIM_HANDSTAND_JUMP, AIR_STEP_CHECK_LEDGE_GRAB);
    return FALSE;
}

s32 act_vertical_wind(struct MarioState *m) {
    s16 intendedDYaw = m->intendedYaw - m->faceAngle[1];
    f32 intendedMag = m->intendedMag / 32.0f;

    play_sound_if_no_flag(m, SOUND_MARIO_HERE_WE_GO, MARIO_MARIO_SOUND_PLAYED);
    if (m->actionState == ACT_STATE_VERTICAL_WIND_SPINNING) {
        set_mario_animation(m, MARIO_ANIM_FORWARD_SPINNING_FLIP);
        if (m->marioObj->header.gfx.animInfo.animFrame == 1) {
            play_sound(SOUND_ACTION_SPIN, m->marioObj->header.gfx.cameraToObject);
#if ENABLE_RUMBLE
            queue_rumble_data(8, 80);
#endif
        }

        if (is_anim_past_end(m)) {
            m->actionState = ACT_STATE_VERTICAL_WIND_AIRBORNE;
        }
    } else {
        set_mario_animation(m, MARIO_ANIM_AIRBORNE_ON_STOMACH);
    }

    update_air_without_turn(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_LANDED:
            set_mario_action(m, ACT_DIVE_SLIDE, 0);
            break;

        case AIR_STEP_HIT_WALL:
            mario_set_forward_vel(m, -16.0f);
            break;
    }

    m->marioObj->header.gfx.angle[0] = (s16)(6144.0f * intendedMag * coss(intendedDYaw));
    m->marioObj->header.gfx.angle[2] = (s16)(-4096.0f * intendedMag * sins(intendedDYaw));
    return FALSE;
}

s32 act_special_triple_jump(struct MarioState *m) {
    if (m->input & INPUT_B_PRESSED) {
        return set_mario_action(m, ACT_DIVE, 0);
    }

    if (m->input & INPUT_Z_PRESSED) {
        return set_mario_action(m, ACT_GROUND_POUND, 0);
    }

    play_mario_sound(m, SOUND_ACTION_TERRAIN_JUMP, SOUND_MARIO_YAHOO);

    update_air_without_turn(m);

    switch (perform_air_step(m, 0)) {
        case AIR_STEP_LANDED:
            if (m->actionState++ == ACT_STATE_SPECIAL_TRIPLE_JUMP_SPINNING) {
                m->vel[1] = 42.0f;
            } else {
                set_mario_action(m, ACT_FREEFALL_LAND_STOP, 0);
            }
            play_mario_landing_sound(m, SOUND_ACTION_TERRAIN_LANDING);
            break;

        case AIR_STEP_HIT_WALL:
            mario_bonk_reflection(m, TRUE);
            break;
    }

    if (m->actionState == ACT_STATE_SPECIAL_TRIPLE_JUMP_SPINNING || m->vel[1] > 0.0f) {
        if (set_mario_animation(m, MARIO_ANIM_FORWARD_SPINNING) == 0) {
            play_sound(SOUND_ACTION_SPIN, m->marioObj->header.gfx.cameraToObject);
        }
    } else {
        set_mario_animation(m, MARIO_ANIM_GENERAL_FALL);
    }

    m->particleFlags |= PARTICLE_SPARKLES;
    return FALSE;
}

s32 check_common_airborne_cancels(struct MarioState *m) {
    if (m->pos[1] < m->waterLevel - 100) {
        return set_water_plunge_action(m);
    }

    if (m->input & INPUT_SQUISHED) {
        return drop_and_set_mario_action(m, ACT_SQUISHED, 0);
    }

#ifdef WIND_RESISTANT_METAL_CAP
    if (!(m->flags & MARIO_METAL_CAP) && SURFACE_IS_VERTICAL_WIND(m->floor->type) && (m->action & ACT_FLAG_ALLOW_VERTICAL_WIND_ACTION)) {
#else
    if (SURFACE_IS_VERTICAL_WIND(m->floor->type) && (m->action & ACT_FLAG_ALLOW_VERTICAL_WIND_ACTION)) {
#endif
        return drop_and_set_mario_action(m, ACT_VERTICAL_WIND, 0);
    }

    m->quicksandDepth = 0.0f;
    return FALSE;
}

s32 mario_execute_airborne_action(struct MarioState *m) {
    u32 cancel = FALSE;

    if (check_common_airborne_cancels(m)) {
        return TRUE;
    }

#ifndef NO_FALL_DAMAGE_SOUND
    play_far_fall_sound(m);
#endif

    /* clang-format off */
    switch (m->action) {
        case ACT_JUMP:                 cancel = act_jump(m);                 break;
        case ACT_DOUBLE_JUMP:          cancel = act_double_jump(m);          break;
        case ACT_FREEFALL:             cancel = act_freefall(m);             break;
        case ACT_HOLD_JUMP:            cancel = act_hold_jump(m);            break;
        case ACT_HOLD_FREEFALL:        cancel = act_hold_freefall(m);        break;
        case ACT_SIDE_FLIP:            cancel = act_side_flip(m);            break;
        case ACT_WALL_KICK_AIR:        cancel = act_wall_kick_air(m);        break;
        case ACT_TWIRLING:             cancel = act_twirling(m);             break;
        case ACT_WATER_JUMP:           cancel = act_water_jump(m);           break;
        case ACT_HOLD_WATER_JUMP:      cancel = act_hold_water_jump(m);      break;
        case ACT_STEEP_JUMP:           cancel = act_steep_jump(m);           break;
        case ACT_BURNING_JUMP:         cancel = act_burning_jump(m);         break;
        case ACT_BURNING_FALL:         cancel = act_burning_fall(m);         break;
        case ACT_TRIPLE_JUMP:          cancel = act_triple_jump(m);          break;
        case ACT_BACKFLIP:             cancel = act_backflip(m);             break;
        case ACT_LONG_JUMP:            cancel = act_long_jump(m);            break;
        case ACT_RIDING_SHELL_JUMP:
        case ACT_RIDING_SHELL_FALL:    cancel = act_riding_shell_air(m);     break;
        case ACT_DIVE:                 cancel = act_dive(m);                 break;
        case ACT_AIR_THROW:            cancel = act_air_throw(m);            break;
        case ACT_BACKWARD_AIR_KB:      cancel = act_backward_air_kb(m);      break;
        case ACT_FORWARD_AIR_KB:       cancel = act_forward_air_kb(m);       break;
        case ACT_HARD_FORWARD_AIR_KB:  cancel = act_hard_forward_air_kb(m);  break;
        case ACT_HARD_BACKWARD_AIR_KB: cancel = act_hard_backward_air_kb(m); break;
        case ACT_SOFT_BONK:            cancel = act_soft_bonk(m);            break;
        case ACT_AIR_HIT_WALL:         cancel = act_air_hit_wall(m);         break;
        case ACT_FORWARD_ROLLOUT:      cancel = act_forward_rollout(m);      break;
        case ACT_SHOT_FROM_CANNON:     cancel = act_shot_from_cannon(m);     break;
        case ACT_BUTT_SLIDE_AIR:       cancel = act_butt_slide_air(m);       break;
        case ACT_HOLD_BUTT_SLIDE_AIR:  cancel = act_hold_butt_slide_air(m);  break;
        case ACT_LAVA_BOOST:           cancel = act_lava_boost(m);           break;
        case ACT_GETTING_BLOWN:        cancel = act_getting_blown(m);        break;
        case ACT_BACKWARD_ROLLOUT:     cancel = act_backward_rollout(m);     break;
        case ACT_CRAZY_BOX_BOUNCE:     cancel = act_crazy_box_bounce(m);     break;
        case ACT_SPECIAL_TRIPLE_JUMP:  cancel = act_special_triple_jump(m);  break;
        case ACT_GROUND_POUND:         cancel = act_ground_pound(m);         break;
        case ACT_THROWN_FORWARD:       cancel = act_thrown_forward(m);       break;
        case ACT_THROWN_BACKWARD:      cancel = act_thrown_backward(m);      break;
        case ACT_FLYING_TRIPLE_JUMP:   cancel = act_flying_triple_jump(m);   break;
        case ACT_SLIDE_KICK:           cancel = act_slide_kick(m);           break;
        case ACT_JUMP_KICK:            cancel = act_jump_kick(m);            break;
        case ACT_FLYING:               cancel = act_flying(m);               break;
        case ACT_RIDING_HOOT:          cancel = act_riding_hoot(m);          break;
        case ACT_TOP_OF_POLE_JUMP:     cancel = act_top_of_pole_jump(m);     break;
        case ACT_VERTICAL_WIND:        cancel = act_vertical_wind(m);        break;
    }
    /* clang-format on */

    return cancel;
}
