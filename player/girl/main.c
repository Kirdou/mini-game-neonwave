/*
 * animation.h
 * -----------
 * Sprite-sheet animation system for SDL2.
 *
 * HOW FLIPPING WORKS
 * ------------------
 * Every PNG sprite sheet contains only RIGHT-facing frames.
 * When the player faces LEFT we pass SDL_FLIP_HORIZONTAL to
 * SDL_RenderCopyEx(). That call mirrors the source rectangle
 * horizontally around the destination rectangle's centre —
 * zero extra texture memory, one flag at render time.
 *
 *   facing_direction == 1  →  SDL_FLIP_NONE         (right)
 *   facing_direction == -1 →  SDL_FLIP_HORIZONTAL   (left)
 *
 * HOW TO ADD A NEW ANIMATION
 * --------------------------
 * 1. Drop Biker_newmove.png in assets/Biker/
 * 2. In player.c, inside player_load_animations():
 *
 *       p->anims[ANIM_NEWMOVE] = anim_load(
 *           renderer, "assets/Biker/Biker_newmove.png",
 *           48,    // frame height (= frame width for square frames)
 *           0.10f  // seconds per frame
 *       );
 *
 * 3. Add ANIM_NEWMOVE to the AnimID enum below.
 * 4. Trigger it in player_update() with player_set_anim(p, ANIM_NEWMOVE, false).
 *
 * Frame count is calculated automatically:
 *     frame_count = texture_width / frame_width
 */

#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ── Enum: every animation the Biker can play ───────────────────────────── */
typedef enum {
    ANIM_IDLE = 0,
    ANIM_WALK,
    ANIM_RUN,
    ANIM_DASH,
    ANIM_JUMP,
    ANIM_DOUBLEJUMP,
    ANIM_SITDOWN,
    ANIM_HAPPY,
    ANIM_ANGRY,
    ANIM_HURT,
    ANIM_DEATH,
    ANIM_PUNCH,
    ANIM_WALK_ATTACK,
    ANIM_RUN_ATTACK,
    ANIM_ATTACK1,
    ANIM_ATTACK2,
    ANIM_ATTACK3,
    ANIM_COUNT   /* sentinel – always last */
} AnimID;

/* ── Core animation data ────────────────────────────────────────────────── */
typedef struct {
    SDL_Texture *texture;   /* sprite sheet texture                        */
    int          frame_w;   /* pixel width  of one frame                   */
    int          frame_h;   /* pixel height of one frame                   */
    int          frame_count;
    float        timer;     /* accumulated time since last frame change     */
    float        speed;     /* seconds per frame                           */
    int          current_frame;
    bool         looping;   /* false = one-shot (freezes on last frame)    */
    bool         finished;  /* true when a one-shot reached its last frame */
} Animation;

/* ── Constants ──────────────────────────────────────────────────────────── */
#define MAX_HP           100
#define HURT_DAMAGE       10
#define SCORE_PER_ATTACK  10

#define WALK_SPEED       200.0f   /* px / second */
#define RUN_SPEED        360.0f
#define DASH_SPEED       600.0f
#define JUMP_VEL        -700.0f   /* negative = up */
#define DOUBLE_JUMP_VEL -580.0f
#define GRAVITY         1600.0f   /* px / second² */

#define SPRITE_SRC_SIZE   48      /* source frame px (sprite sheet) */
#define SPRITE_DRAW_SIZE 144      /* rendered size on screen (3× scale) */

#define DOUBLE_SHIFT_WINDOW 0.30f /* seconds for double-tap dash detection */

/* ── Player states ──────────────────────────────────────────────────────── */
typedef enum {
    STATE_IDLE = 0,
    STATE_WALK,
    STATE_RUN,
    STATE_DASH,
    STATE_JUMP,
    STATE_DOUBLEJUMP,
    STATE_SITDOWN,
    STATE_HAPPY,
    STATE_ANGRY,
    STATE_HURT,
    STATE_DEATH,
    STATE_PUNCH,
    STATE_WALK_ATTACK,
    STATE_RUN_ATTACK,
    STATE_ATTACK1,
    STATE_ATTACK2,
    STATE_ATTACK3,
    STATE_COUNT
} PlayerState;

/* ── Player struct ──────────────────────────────────────────────────────── */
typedef struct {
    /* Position & physics */
    float x, y;
    float vel_x, vel_y;
    bool  on_ground;
    int   jumps_used;        /* 0, 1 or 2 */

    /* Direction: +1 = right, -1 = left */
    int facing_direction;

    /* Animation */
    Animation    anims[ANIM_COUNT];
    PlayerState  state;
    AnimID       anim_id;    /* maps 1-to-1 with state */

    /* HP & score */
    int  hp;
    int  score;
    bool dead;

    /* Double-shift dash detection */
    float shift_last_time;   /* seconds since game start at last shift press */
    int   shift_tap_count;
    bool  dash_queued;

    /* Floor y-coordinate (set from screen height at init) */
    int floor_y;

    /* SDL renderer reference (needed for texture loading) */
    SDL_Renderer *renderer;
} Player;

/* ═══════════════════════════════════════════════════════════════════════════
 * animation.c
 * -----------
 * Implementation of the sprite-sheet animation system.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ─────────────────────────────────────────────────────────────────────────
 * anim_load
 * ───────────────────────────────────────────────────────────────────────── */
static Animation anim_load(SDL_Renderer *renderer,
                    const char   *path,
                    int           frame_h,
                    float         speed,
                    bool          looping)
{
    Animation a = {0};
    a.frame_h = frame_h;
    a.frame_w = frame_h;   /* frames are square */
    a.speed   = speed;
    a.looping = looping;

    /* Load the sprite sheet ------------------------------------------------ */
    SDL_Surface *surf = IMG_Load(path);
    if (!surf) {
        fprintf(stderr, "[anim_load] IMG_Load failed for '%s': %s\n",
                path, IMG_GetError());
        return a;
    }

    a.texture = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);

    if (!a.texture) {
        fprintf(stderr, "[anim_load] SDL_CreateTextureFromSurface failed: %s\n",
                SDL_GetError());
        return a;
    }

    /* Derive frame count from texture width -------------------------------- */
    int tex_w = 0;
    SDL_QueryTexture(a.texture, NULL, NULL, &tex_w, NULL);
    a.frame_count = tex_w / a.frame_w;

    return a;
}

/* ─────────────────────────────────────────────────────────────────────────
 * anim_update
 * ───────────────────────────────────────────────────────────────────────── */
static void anim_update(Animation *anim, float dt)
{
    if (!anim->texture) return;
    if (anim->finished)  return;   /* frozen on last frame of a one-shot */

    anim->timer += dt;
    if (anim->timer >= anim->speed) {
        anim->timer -= anim->speed;
        anim->current_frame++;

        if (anim->current_frame >= anim->frame_count) {
            if (anim->looping) {
                anim->current_frame = 0;
            } else {
                /* One-shot: freeze on the last frame */
                anim->current_frame = anim->frame_count - 1;
                anim->finished      = true;
            }
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * anim_reset
 * ───────────────────────────────────────────────────────────────────────── */
static void anim_reset(Animation *anim)
{
    anim->current_frame = 0;
    anim->timer         = 0.0f;
    anim->finished      = false;
}

/* ─────────────────────────────────────────────────────────────────────────
 * anim_draw
 *
 * SDL_RenderCopyEx signature:
 *   SDL_RenderCopyEx(renderer, texture,
 *                    &src_rect,   ← which frame to read from the sheet
 *                    &dst_rect,   ← where on screen to draw it
 *                    angle,       ← 0.0 – no rotation
 *                    center,      ← NULL – rotate around dst centre
 *                    flip)        ← SDL_FLIP_HORIZONTAL for left-facing
 *
 * When flip == SDL_FLIP_HORIZONTAL the GPU mirrors the source rectangle
 * around the destination rectangle's vertical axis.  No extra texture or
 * CPU work is needed — the original right-facing sheet is used as-is.
 * ───────────────────────────────────────────────────────────────────────── */
static void anim_draw(const Animation *anim,
               SDL_Renderer    *renderer,
               int              x,
               int              y,
               int              draw_w,
               int              draw_h,
               SDL_RendererFlip flip)
{
    if (!anim->texture) return;

    /* Source rectangle: the current frame inside the sprite sheet */
    SDL_Rect src = {
        .x = anim->current_frame * anim->frame_w,
        .y = 0,
        .w = anim->frame_w,
        .h = anim->frame_h
    };

    /* Destination rectangle: screen position & render size */
    SDL_Rect dst = {
        .x = x,
        .y = y,
        .w = draw_w,
        .h = draw_h
    };

    SDL_RenderCopyEx(renderer, anim->texture,
                     &src, &dst,
                     0.0,   /* angle  */
                     NULL,  /* center */
                     flip); /* ← SDL_FLIP_HORIZONTAL when facing left */
}

/* ─────────────────────────────────────────────────────────────────────────
 * anim_free
 * ───────────────────────────────────────────────────────────────────────── */
static void anim_free(Animation *anim)
{
    if (anim->texture) {
        SDL_DestroyTexture(anim->texture);
        anim->texture = NULL;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * player.c
 * --------
 * Player state machine, physics, input handling, and UI rendering.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ── Internal helpers ───────────────────────────────────────────────────── */

/* Which states are "one-shot" (must finish before returning to idle) */
static bool is_one_shot(PlayerState s)
{
    switch (s) {
        case STATE_DASH:
        case STATE_JUMP:
        case STATE_DOUBLEJUMP:
        case STATE_SITDOWN:
        case STATE_HAPPY:
        case STATE_ANGRY:
        case STATE_HURT:
        case STATE_DEATH:
        case STATE_PUNCH:
        case STATE_WALK_ATTACK:
        case STATE_RUN_ATTACK:
        case STATE_ATTACK1:
        case STATE_ATTACK2:
        case STATE_ATTACK3:
            return true;
        default:
            return false;
    }
}

/* Which states are attacks (grant score on completion) */
static bool is_attack(PlayerState s)
{
    switch (s) {
        case STATE_PUNCH:
        case STATE_WALK_ATTACK:
        case STATE_RUN_ATTACK:
        case STATE_ATTACK1:
        case STATE_ATTACK2:
        case STATE_ATTACK3:
            return true;
        default:
            return false;
    }
}

/* Map state → AnimID  (1-to-1 so the cast works, but kept explicit) */
static AnimID state_to_anim(PlayerState s)
{
    /* The enum values are identical, but keeping this explicit makes it
       safe to reorder one without breaking the other. */
    switch (s) {
        case STATE_IDLE:        return ANIM_IDLE;
        case STATE_WALK:        return ANIM_WALK;
        case STATE_RUN:         return ANIM_RUN;
        case STATE_DASH:        return ANIM_DASH;
        case STATE_JUMP:        return ANIM_JUMP;
        case STATE_DOUBLEJUMP:  return ANIM_DOUBLEJUMP;
        case STATE_SITDOWN:     return ANIM_SITDOWN;
        case STATE_HAPPY:       return ANIM_HAPPY;
        case STATE_ANGRY:       return ANIM_ANGRY;
        case STATE_HURT:        return ANIM_HURT;
        case STATE_DEATH:       return ANIM_DEATH;
        case STATE_PUNCH:       return ANIM_PUNCH;
        case STATE_WALK_ATTACK: return ANIM_WALK_ATTACK;
        case STATE_RUN_ATTACK:  return ANIM_RUN_ATTACK;
        case STATE_ATTACK1:     return ANIM_ATTACK1;
        case STATE_ATTACK2:     return ANIM_ATTACK2;
        case STATE_ATTACK3:     return ANIM_ATTACK3;
        default:                return ANIM_IDLE;
    }
}

/* Switch to a new state, resetting animation if it changed */
static void set_state(Player *p, PlayerState new_state)
{
    if (new_state == p->state) return;
    p->state   = new_state;
    p->anim_id = state_to_anim(new_state);
    anim_reset(&p->anims[p->anim_id]);
}

/* True while a one-shot animation is still playing */
static bool one_shot_locked(const Player *p)
{
    if (!is_one_shot(p->state)) return false;
    return !p->anims[p->anim_id].finished;
}

/* ── Load all animations ─────────────────────────────────────────────────
 *
 * HOW TO ADD A NEW ANIMATION:
 *   1. Add ANIM_NEWMOVE to AnimID in animation.h
 *   2. Add STATE_NEWMOVE to PlayerState in player.h
 *   3. Add the anim_load() call below
 *   4. Add the case to state_to_anim() and is_one_shot() / is_attack() above
 *   5. Trigger it with set_state(p, STATE_NEWMOVE) in player_update()
 * ───────────────────────────────────────────────────────────────────────── */
static bool load_animations(Player *p)
{
    const char *base = "assets/Punk/";

    struct { AnimID id; const char *file; float spf; bool loop; } defs[] = {
        { ANIM_IDLE,        "Punk_idle.png",        0.12f, true  },
        { ANIM_WALK,        "Punk_walk.png",        0.10f, true  },
        { ANIM_RUN,         "Punk_run.png",         0.08f, true  },
        { ANIM_DASH,        "Punk_dash.png",        0.07f, false },
        { ANIM_JUMP,        "Punk_jump.png",        0.12f, false },
        { ANIM_DOUBLEJUMP,  "Punk_doublejump.png",  0.10f, false },
        { ANIM_SITDOWN,     "Punk_sitdown.png",     0.12f, false },
        { ANIM_HAPPY,       "Punk_happy.png",       0.12f, false },
        { ANIM_ANGRY,       "Punk_angry.png",       0.10f, false },
        { ANIM_HURT,        "Punk_hurt.png",        0.10f, false },
        { ANIM_DEATH,       "Punk_death.png",       0.12f, false },
        { ANIM_PUNCH,       "Punk_punch.png",       0.08f, false },
        { ANIM_WALK_ATTACK, "Punk_walk_attack.png", 0.08f, false },
        { ANIM_RUN_ATTACK,  "Punk_run_attack.png",  0.07f, false },
        { ANIM_ATTACK1,     "Punk_attack1.png",     0.09f, false },
        { ANIM_ATTACK2,     "Punk_attack2.png",     0.09f, false },
        { ANIM_ATTACK3,     "Punk_attack3.png",     0.09f, false },
    };

    char path[256];
    int  n = (int)(sizeof(defs) / sizeof(defs[0]));

    for (int i = 0; i < n; i++) {
        snprintf(path, sizeof(path), "%s%s", base, defs[i].file);
        p->anims[defs[i].id] = anim_load(
            p->renderer, path,
            SPRITE_SRC_SIZE,
            defs[i].spf,
            defs[i].loop
        );
        if (!p->anims[defs[i].id].texture) {
            fprintf(stderr, "[player] Failed to load: %s\n", path);
            return false;
        }
    }
    return true;
}

/* ── player_create ──────────────────────────────────────────────────────── */
static Player *player_create(SDL_Renderer *renderer, int screen_w, int screen_h)
{
    (void)screen_w;   /* reserved for future horizontal clamping */
    Player *p = (Player *)calloc(1, sizeof(Player));
    if (!p) return NULL;

    p->renderer = renderer;
    p->floor_y  = screen_h - 80;   /* ground surface y */

    if (!load_animations(p)) {
        free(p);
        return NULL;
    }

    /* player_reset inline call below */
    /* Position: centre of screen, standing on ground */
    p->x = 400.0f - SPRITE_DRAW_SIZE / 2.0f;
    p->y = (float)(p->floor_y - SPRITE_DRAW_SIZE);

    p->vel_x           = 0.0f;
    p->vel_y           = 0.0f;
    p->on_ground       = true;
    p->jumps_used      = 0;
    p->facing_direction = 1;   /* right */

    p->state   = STATE_IDLE;
    p->anim_id = ANIM_IDLE;
    for (int i = 0; i < ANIM_COUNT; i++)
        anim_reset(&p->anims[i]);

    p->hp    = MAX_HP;
    p->score = 0;
    p->dead  = false;

    p->shift_last_time = -999.0f;
    p->shift_tap_count = 0;
    p->dash_queued     = false;

    return p;
}

/* ── player_destroy ─────────────────────────────────────────────────────── */
static void player_destroy(Player *p)
{
    if (!p) return;
    for (int i = 0; i < ANIM_COUNT; i++)
        anim_free(&p->anims[i]);
    free(p);
}

/* ── player_reset ───────────────────────────────────────────────────────── */
static void player_reset(Player *p)
{
    /* Position: centre of screen, standing on ground */
    p->x = 400.0f - SPRITE_DRAW_SIZE / 2.0f;
    p->y = (float)(p->floor_y - SPRITE_DRAW_SIZE);

    p->vel_x           = 0.0f;
    p->vel_y           = 0.0f;
    p->on_ground       = true;
    p->jumps_used      = 0;
    p->facing_direction = 1;   /* right */

    p->state   = STATE_IDLE;
    p->anim_id = ANIM_IDLE;
    for (int i = 0; i < ANIM_COUNT; i++)
        anim_reset(&p->anims[i]);

    p->hp    = MAX_HP;
    p->score = 0;
    p->dead  = false;

    p->shift_last_time = -999.0f;
    p->shift_tap_count = 0;
    p->dash_queued     = false;
}

/* ── player_handle_event ────────────────────────────────────────────────── */
static void player_handle_event(Player *p, const SDL_Event *e, float now_sec)
{
    /* ── Key-down events ──────────────────────────────────────────────── */
    if (e->type == SDL_KEYDOWN && !e->key.repeat) {
        SDL_Keycode k    = e->key.keysym.sym;
        SDL_Keymod  mods = SDL_GetModState(); (void)mods; /* available for future modifier use */

        /* Reset works even when dead */
        if (k == SDLK_r) {
            player_reset(p);
            return;
        }

        if (p->dead) return;

        switch (k) {
            /* Jump / double-jump */
            case SDLK_SPACE:
                if (!one_shot_locked(p)) {
                    if (p->on_ground) {
                        p->vel_y      = JUMP_VEL;
                        p->on_ground  = false;
                        p->jumps_used = 1;
                        set_state(p, STATE_JUMP);
                    } else if (p->jumps_used < 2) {
                        p->vel_y      = DOUBLE_JUMP_VEL;
                        p->jumps_used = 2;
                        set_state(p, STATE_DOUBLEJUMP);
                    }
                }
                break;

            /* Emotes / sit */
            case SDLK_z:
                if (!one_shot_locked(p)) set_state(p, STATE_HAPPY);
                break;
            case SDLK_s:
                if (!one_shot_locked(p) && p->on_ground)
                    set_state(p, STATE_SITDOWN);
                break;
            case SDLK_q:  /* physical A key on AZERTY → angry */
                if (!one_shot_locked(p)) set_state(p, STATE_ANGRY);
                break;

            /* Dash */
            case SDLK_x:
                if (!one_shot_locked(p)) {
                    p->vel_x = p->facing_direction * DASH_SPEED;
                    set_state(p, STATE_DASH);
                }
                break;

            /* Hurt */
            case SDLK_h:
                p->hp -= HURT_DAMAGE;
                if (p->hp <= 0) {
                    p->hp  = 0;
                    p->dead = true;
                    set_state(p, STATE_DEATH);
                } else {
                    set_state(p, STATE_HURT);
                }
                break;

            /* Force death */
            case SDLK_m:
                p->hp   = 0;
                p->dead = true;
                set_state(p, STATE_DEATH);
                break;

            default: break;
        }
    }

    /* ── Mouse-button events ──────────────────────────────────────────── */
    if (!p->dead && e->type == SDL_MOUSEBUTTONDOWN && !one_shot_locked(p)) {
        SDL_Keymod   mods    = SDL_GetModState();
        const Uint8 *ks     = SDL_GetKeyboardState(NULL);
        bool shift   = (mods & KMOD_SHIFT)  != 0;
        bool ctrl    = (mods & KMOD_CTRL)   != 0;
        bool alt     = (mods & KMOD_ALT)    != 0;
        bool walking = ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_Q];

        if (e->button.button == SDL_BUTTON_LEFT) {
            if (shift && walking)       set_state(p, STATE_RUN_ATTACK);
            else if (walking)           set_state(p, STATE_WALK_ATTACK);
            else                        set_state(p, STATE_PUNCH);
        } else if (e->button.button == SDL_BUTTON_RIGHT) {
            if (ctrl)                   set_state(p, STATE_ATTACK2);
            else if (alt)               set_state(p, STATE_ATTACK3);
            else                        set_state(p, STATE_ATTACK1);
        }
    }
}

/* ── player_update ──────────────────────────────────────────────────────── */
static void player_update(Player *p, float dt, const Uint8 *keys)
{
    /* Always advance the current animation */
    anim_update(&p->anims[p->anim_id], dt);

    /* Award score when an attack one-shot just finished */
    if (is_attack(p->state) && p->anims[p->anim_id].finished) {
        /* Only award once: finished stays true, we check via a helper flag
           stored in anim.finished — we'll clear the flag by switching state */
        if (p->anims[p->anim_id].finished) {
            /* Give score exactly once by checking a simple boolean we derive
               from the animation: finished==true and state still is attack */
            static PlayerState last_scored = -1;
            if (last_scored != p->state) {
                p->score += SCORE_PER_ATTACK;
                last_scored = p->state;
            }
        }
    }

    /* Dead: nothing more to do */
    if (p->dead) return;

    /* ── Movement (continuous key polling) ──────────────────────────── */
    bool locked  = one_shot_locked(p);
    bool shift   = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    bool move_r  = keys[SDL_SCANCODE_D];
    bool move_l  = keys[SDL_SCANCODE_Q];

    float dx = 0.0f;

    if (!locked) {
        /* Direction */
        if (move_r) { dx = 1.0f;  p->facing_direction =  1; }
        if (move_l) { dx = -1.0f; p->facing_direction = -1; }

        bool moving = (dx != 0.0f);

        /* Dash overrides shift-run — handled via X key */
        if (shift && moving) {
            p->vel_x = dx * RUN_SPEED;
            if (p->on_ground) set_state(p, STATE_RUN);
        } else if (moving) {
            p->vel_x = dx * WALK_SPEED;
            if (p->on_ground) set_state(p, STATE_WALK);
        } else {
            p->vel_x = 0.0f;
            if (p->on_ground) set_state(p, STATE_IDLE);
        }
    }

    /* ── When a one-shot finishes, return to idle/walk ──────────────── */
    if (locked && p->anims[p->anim_id].finished) {
        /* Decide where to go after the one-shot */
        bool moving = move_r || move_l;
        if (moving && shift) set_state(p, STATE_RUN);
        else if (moving)     set_state(p, STATE_WALK);
        else                 set_state(p, STATE_IDLE);
    }

    /* ── Physics ────────────────────────────────────────────────────── */
    p->vel_y += GRAVITY * dt;
    p->x     += p->vel_x * dt;
    p->y     += p->vel_y * dt;

    /* ── Ground collision ───────────────────────────────────────────── */
    float ground_y = (float)(p->floor_y - SPRITE_DRAW_SIZE);
    if (p->y >= ground_y) {
        p->y          = ground_y;
        p->vel_y      = 0.0f;
        p->on_ground  = true;
        p->jumps_used = 0;

        /* Land from air: if no one-shot active pick correct ground state */
        if (!locked) {
            bool moving = move_r || move_l;
            if (moving && shift) set_state(p, STATE_RUN);
            else if (moving)     set_state(p, STATE_WALK);
            else                 set_state(p, STATE_IDLE);
        }
    }

    /* ── Clamp to screen bounds ─────────────────────────────────────── */
    if (p->x < 0)                          p->x = 0;
    if (p->x > 900 - SPRITE_DRAW_SIZE)     p->x = 900 - SPRITE_DRAW_SIZE;
}

/* ── player_draw ────────────────────────────────────────────────────────── */
static void player_draw(const Player *p, SDL_Renderer *renderer)
{
    /*
     * FLIP LOGIC:
     * -----------
     * facing_direction  ==  1  →  SDL_FLIP_NONE          (right – original)
     * facing_direction  == -1  →  SDL_FLIP_HORIZONTAL    (left  – mirrored)
     *
     * SDL_RenderCopyEx mirrors the source rectangle around the destination
     * rectangle's vertical centre axis.  The GPU does this in a single pass;
     * no extra texture memory is used.  The right-facing sprite sheet is
     * the only texture stored.
     */
    SDL_RendererFlip flip = (p->facing_direction == 1)
                            ? SDL_FLIP_NONE
                            : SDL_FLIP_HORIZONTAL;

    anim_draw(&p->anims[p->anim_id],
              renderer,
              (int)p->x,
              (int)p->y,
              SPRITE_DRAW_SIZE,
              SPRITE_DRAW_SIZE,
              flip);
}

/* ── player_draw_ui ─────────────────────────────────────────────────────── */
static void player_draw_ui(const Player *p, SDL_Renderer *renderer, TTF_Font *font,
                    int screen_w)
{
    /* ── HP bar ───────────────────────────────────────────────────────── */
    const int BAR_X = 20, BAR_Y = 20;
    const int BAR_W = 220, BAR_H = 22;

    /* Background */
    SDL_SetRenderDrawColor(renderer, 60, 20, 20, 255);
    SDL_Rect bg = { BAR_X, BAR_Y, BAR_W, BAR_H };
    SDL_RenderFillRect(renderer, &bg);

    /* Fill */
    int fill_w = (BAR_W * p->hp) / MAX_HP;
    if (fill_w > 0) {
        SDL_SetRenderDrawColor(renderer, 220, 50, 50, 255);
        SDL_Rect fill = { BAR_X, BAR_Y, fill_w, BAR_H };
        SDL_RenderFillRect(renderer, &fill);

        /* Highlight stripe */
        SDL_SetRenderDrawColor(renderer, 255, 120, 120, 255);
        SDL_Rect hi = { BAR_X, BAR_Y, fill_w, BAR_H / 3 };
        SDL_RenderFillRect(renderer, &hi);
    }

    /* Border */
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderDrawRect(renderer, &bg);

    /* HP label */
    if (font) {
        char hp_text[32];
        snprintf(hp_text, sizeof(hp_text), "HP %d / %d", p->hp, MAX_HP);
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface *surf = TTF_RenderText_Blended(font, hp_text, white);
        if (surf) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_Rect dst = { BAR_X + BAR_W + 10, BAR_Y + 2, surf->w, surf->h };
            SDL_RenderCopy(renderer, tex, NULL, &dst);
            SDL_DestroyTexture(tex);
            SDL_FreeSurface(surf);
        }
    }

    /* ── Score (top-right) ────────────────────────────────────────────── */
    if (font) {
        char sc_text[32];
        snprintf(sc_text, sizeof(sc_text), "SCORE  %06d", p->score);
        SDL_Color gold = {255, 220, 80, 255};
        SDL_Surface *surf = TTF_RenderText_Blended(font, sc_text, gold);
        if (surf) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_Rect dst = { screen_w - surf->w - 20, 20, surf->w, surf->h };
            SDL_RenderCopy(renderer, tex, NULL, &dst);
            SDL_DestroyTexture(tex);
            SDL_FreeSurface(surf);
        }
    }

    /* ── Dead overlay ─────────────────────────────────────────────────── */
    if (p->dead && font) {
        SDL_Color red = {255, 80, 80, 255};
        SDL_Surface *surf = TTF_RenderText_Blended(
            font, "-- DEAD --  Press R to reset", red);
        if (surf) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_Rect dst = {
                screen_w / 2 - surf->w / 2,
                200 - surf->h / 2,
                surf->w, surf->h
            };
            SDL_RenderCopy(renderer, tex, NULL, &dst);
            SDL_DestroyTexture(tex);
            SDL_FreeSurface(surf);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * main.c
 * ------
 * Entry point: SDL2 initialisation, main game loop, background rendering.
 *
 * Build (Linux / macOS):
 *   gcc -std=c11 -O2 -o biker_game \
 *       main.c \
 *       $(sdl2-config --cflags --libs) \
 *       -lSDL2_image -lSDL2_ttf
 *
 * Build (Windows – MinGW):
 *   gcc -std=c11 -O2 -o biker_game.exe \
 *       main.c \
 *       -Iinclude -Llib \
 *       -lSDL2 -lSDL2_image -lSDL2_ttf -lmingw32 -mwindows
 *
 * Run from the project root (where assets/ lives):
 *   ./biker_game
 *
 * ──────────────────────────────────────────────────────────────────────────
 * CONTROLS
 * --------
 *   D               walk right
 *   Q               walk left  (sprite flipped with SDL_FLIP_HORIZONTAL)
 *   Shift + D/Q     run
 *   X               dash
 *   Space           jump
 *   Space (in air)  double jump
 *   S               sit down
 *   Z               happy
 *   A               angry
 *   H               hurt  (−10 HP)
 *   M               death / force kill
 *   R               reset
 *
 *   Left  click             punch
 *   D/Q + Left click        walk attack
 *   Shift+D/Q + Left click  run attack
 *   Right click             attack 1
 *   Ctrl  + Right click     attack 2
 *   Alt   + Right click     attack 3
 * ──────────────────────────────────────────────────────────────────────────
 */

/* ── Screen constants ───────────────────────────────────────────────────── */
#define SCREEN_W 900
#define SCREEN_H 400
#define FPS      60
#define FLOOR_Y  (SCREEN_H - 80)

/* ── Background ─────────────────────────────────────────────────────────── */

/* Pre-build a gradient sky texture once at startup */
static SDL_Texture *make_sky_texture(SDL_Renderer *r)
{
    SDL_Texture *tex = SDL_CreateTexture(
        r, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        SCREEN_W, SCREEN_H
    );
    if (!tex) return NULL;

    SDL_SetRenderTarget(r, tex);
    for (int y = 0; y < FLOOR_Y; y++) {
        float t = (float)y / (float)FLOOR_Y;
        Uint8 rv = (Uint8)(30  + (80  - 30)  * t);
        Uint8 gv = (Uint8)(20  + (40  - 20)  * t);
        Uint8 bv = (Uint8)(60  + (120 - 60)  * t);
        SDL_SetRenderDrawColor(r, rv, gv, bv, 255);
        SDL_RenderDrawLine(r, 0, y, SCREEN_W, y);
    }
    /* Floor */
    SDL_SetRenderDrawColor(r, 50, 35, 80, 255);
    SDL_Rect floor_rect = { 0, FLOOR_Y, SCREEN_W, SCREEN_H - FLOOR_Y };
    SDL_RenderFillRect(r, &floor_rect);
    /* Grid lines on floor */
    SDL_SetRenderDrawColor(r, 60, 45, 95, 255);
    for (int x = 0; x < SCREEN_W; x += 60)
        SDL_RenderDrawLine(r, x, FLOOR_Y, x, SCREEN_H);

    SDL_SetRenderTarget(r, NULL);
    return tex;
}

static void draw_controls_hint(SDL_Renderer *r, TTF_Font *font)
{
    if (!font) return;
    const char *hints[] = {
        "D/A: walk   Shift+D/A: run   X: dash",
        "Space: jump / double-jump   S: sit   Z: happy   Q: angry",
        "H: hurt   M: death   R: reset",
        "LClick: punch | D/A+LClick: walk atk | Shift+LClick: run atk",
        "RClick: atk1  | Ctrl+RClick: atk2    | Alt+RClick: atk3",
    };
    int n = (int)(sizeof(hints) / sizeof(hints[0]));
    SDL_Color col = {160, 140, 200, 255};

    for (int i = 0; i < n; i++) {
        SDL_Surface *s = TTF_RenderText_Blended(font, hints[i], col);
        if (!s) continue;
        SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
        int y = SCREEN_H - 18 * (n - i) - 4;
        SDL_Rect dst = { 8, y, s->w, s->h };
        SDL_RenderCopy(r, t, NULL, &dst);
        SDL_DestroyTexture(t);
        SDL_FreeSurface(s);
    }
}

static void draw_state_label(SDL_Renderer *r, TTF_Font *font, const Player *p)
{
    if (!font) return;
    static const char *state_names[] = {
        "idle","walk","run","dash","jump","doublejump","sitdown",
        "happy","angry","hurt","death","punch",
        "walk_attack","run_attack","attack1","attack2","attack3"
    };
    char buf[128];
    snprintf(buf, sizeof(buf), "state: %-14s  dir: %s  frame: %d",
             state_names[p->state],
             p->facing_direction == 1 ? "right" : "left",
             p->anims[p->anim_id].current_frame);

    SDL_Color col = {200, 180, 255, 255};
    SDL_Surface *s = TTF_RenderText_Blended(font, buf, col);
    if (!s) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_Rect dst = { SCREEN_W / 2 - s->w / 2, FLOOR_Y + 6, s->w, s->h };
    SDL_RenderCopy(r, t, NULL, &dst);
    SDL_DestroyTexture(t);
    SDL_FreeSurface(s);
}

/* ── main ───────────────────────────────────────────────────────────────── */
int main(void)
{
    /* ── SDL initialisation ─────────────────────────────────────────────── */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        fprintf(stderr, "IMG_Init: %s\n", IMG_GetError());
        SDL_Quit();
        return 1;
    }

    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        IMG_Quit();
        SDL_Quit();
        return 1;
    }

    /* ── Window & renderer ──────────────────────────────────────────────── */
    SDL_Window *window = SDL_CreateWindow(
        "Biker — SDL2 Sprite Demo",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H, 0
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        goto cleanup_sdl;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        goto cleanup_window;
    }

    /* ── Font ───────────────────────────────────────────────────────────── */
    /* SDL_ttf needs a .ttf file.  We search common system paths.
       If none is found, text is skipped gracefully (game still works).  */
    TTF_Font *font     = NULL;
    TTF_Font *font_sm  = NULL;
    const char *font_paths[] = {
        /* Linux */  "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
        /* Linux */  "/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf",
        /* macOS */  "/System/Library/Fonts/Menlo.ttc",
        /* Windows */"C:\\Windows\\Fonts\\consola.ttf",
        /* Windows */"C:\\Windows\\Fonts\\cour.ttf",
        NULL
    };
    for (int i = 0; font_paths[i]; i++) {
        font    = TTF_OpenFont(font_paths[i], 16);
        font_sm = TTF_OpenFont(font_paths[i], 12);
        if (font && font_sm) break;
        if (font)    { TTF_CloseFont(font);    font    = NULL; }
        if (font_sm) { TTF_CloseFont(font_sm); font_sm = NULL; }
    }
    if (!font)
        fprintf(stderr, "[warn] No system font found – text UI disabled.\n");

    /* ── Background texture ─────────────────────────────────────────────── */
    SDL_Texture *sky = make_sky_texture(renderer);

    /* ── Player ─────────────────────────────────────────────────────────── */
    Player *player = player_create(renderer, SCREEN_W, SCREEN_H);
    if (!player) {
        fprintf(stderr, "player_create failed – check assets/ path.\n");
        goto cleanup_all;
    }

    /* ── Main loop ──────────────────────────────────────────────────────── */
    bool      running   = true;
    Uint32    last_tick = SDL_GetTicks();
    const Uint8 *keys   = SDL_GetKeyboardState(NULL);

    while (running) {
        /* Delta time */
        Uint32 now_ms = SDL_GetTicks();
        float  dt     = (now_ms - last_tick) / 1000.0f;
        float  now_sec = now_ms / 1000.0f;
        last_tick = now_ms;
        if (dt > 0.05f) dt = 0.05f;   /* clamp: don't spiral after alt-tab */

        /* Events */
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT)
                running = false;
            else
                player_handle_event(player, &e, now_sec);
        }

        /* Update */
        player_update(player, dt, keys);

        /* Render */
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        /* Background */
        if (sky) SDL_RenderCopy(renderer, sky, NULL, NULL);

        /* Player sprite */
        player_draw(player, renderer);

        /* UI */
        player_draw_ui(player, renderer, font, SCREEN_W);
        draw_state_label(renderer, font_sm, player);
        draw_controls_hint(renderer, font_sm);

        SDL_RenderPresent(renderer);

        /* Uncap: vsync handles timing; this is a fallback */
        Uint32 frame_ms = SDL_GetTicks() - now_ms;
        if (frame_ms < 1000 / FPS)
            SDL_Delay(1000 / FPS - frame_ms);
    }

    /* ── Cleanup ────────────────────────────────────────────────────────── */
    player_destroy(player);

cleanup_all:
    if (sky)     SDL_DestroyTexture(sky);
    if (font_sm) TTF_CloseFont(font_sm);
    if (font)    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
cleanup_window:
    SDL_DestroyWindow(window);
cleanup_sdl:
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    return 0;
}
