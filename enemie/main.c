/*

 * Cyberpunk Enemy System – SDL2 / C

 *

 * PATCH v7 — FOUR TARGETED IMPROVEMENTS (animation/AI untouched):

 *

 * [G1] GROUND ALIGNMENT FIX:

 *    All ground enemies (Dog, Spider, Cyborg) use visual-bottom snapping:

 *     y = GROUND_Y - (frameH * renderScale)

 *    Applied in initEnemyBase(), activateEnemy(), updatePhysics().

 *    cyborgGroundY() already existed; added dogGroundY(), spiderGroundY().

 *    e->onGround = true, e->velY = 0 forced on spawn/respawn.

 *    Drone unchanged (flying).

 *

 * [G2] SCROLL-BASED RESPAWN SYSTEM:

 *    player.worldX tracks furthest X reached (already existed).

 *    SPAWN_TRIGGER_AHEAD (400 px) defines how far ahead of worldX enemies spawn.

 *    spawnX for respawned minions is player.worldX + random offset in

 *    [SPAWN_TRIGGER_AHEAD, SPAWN_TRIGGER_AHEAD+300], clamped to map bounds.

 *    Respawn also gated on player advancing (worldX >= lastSpawnWorldX + SPAWN_STEP).

 *    This replaces the pure timer+kill-count respawn with progression-based spawning.

 *

 * [G3] REALISTIC AGGRESSION (ENGAGE DISTANCE):

 *    Added engageDistance per enemy (smaller than detectRange).

 *    Detection is now a 3-zone system:

 *     > detectRange → patrol only

 *     <= detectRange → chase (follow slowly)

 *     <= engageDistance → attack (if cooldown met)

 *    updateAI() modified: attack only triggers inside engageDistance.

 *

 * [G4] MASS AGGRO PREVENTION:

 *    EM_countAttacking() counts enemies currently in STATE_ATTACK.

 *    MAX_SIMULTANEOUS_ATTACKERS = 2.

 *    In updateAI(), before transitioning to STATE_ATTACK, the system checks

 *    the global attacker count; if >= cap, enemy stays in STATE_CHASE.

 *    Passed as a bool parameter canAttack into updateAI().

 */



#include <SDL2/SDL.h>

#include <SDL2/SDL_image.h>

#include <stdbool.h>

#include <math.h>

#include <stdio.h>

#include <stdlib.h>

#include <string.h>



/* ═══════════════════════════════════════════════════════════════════════════

  Constants

  ═══════════════════════════════════════════════════════════════════════════ */



#define MAX_ENEMIES     32

#define GRAVITY       0.45f

#define GROUND_Y      500



#define SPEED_DOG      1.9f

#define SPEED_DRONE     1.5f

#define SPEED_SPIDER    1.3f

#define SPEED_CYBORG    2.2f



#define DETECT_DOG     320.0f

#define DETECT_DRONE    300.0f

#define DETECT_SPIDER    250.0f

#define DETECT_CYBORG    460.0f



/* [G3] Engage distances — must be within detectRange to actually attack */

#define ENGAGE_DOG     120.0f

#define ENGAGE_DRONE    160.0f

#define ENGAGE_SPIDER    100.0f

#define ENGAGE_CYBORG    160.0f



#define ATTACK_DOG      80.0f

#define ATTACK_DRONE    130.0f

#define ATTACK_SPIDER    70.0f

#define ATTACK_CYBORG    110.0f



#define ATTACK_COOLDOWN_MS  950

#define HURT_DURATION_MS   500

#define STATE_MIN_MS     120

#define PATROL_HALF     220.0f

#define LERP_FACTOR     0.13f

#define ATTACK_WINDUP_MS   220



#define KNOCKBACK_SPEED_ENEMY  5.5f

#define KNOCKBACK_SPEED_PLAYER 2.2f

#define KNOCKBACK_DURATION_MS  160



#define SHAKE_MAGNITUDE_HIT   5

#define SHAKE_MAGNITUDE_BOSS  9

#define SHAKE_DURATION_MS    200



#define WINDOW_W      1280

#define WINDOW_H       720

#define TARGET_FPS      60

#define FRAME_MS      (1000 / TARGET_FPS)



#define ANIM_MIN_INTERRUPTIBLE_MS 100

#define MIN_SPF           30



#define CYBORG_RENDER_SCALE 2.40f

#define DOG_RENDER_SCALE   0.65f

#define SPIDER_RENDER_SCALE 0.72f

#define DRONE_RENDER_SCALE  0.85f



#define MAP_WIDTH      8000.0f

#define ZONE_BOSS_START   0.85f



#define RESPAWN_DELAY_MS   3000

#define RESPAWN_DELAY_MIN  1500



#define CAP_DOG       3

#define CAP_SPIDER      2

#define CAP_DRONE      1



#define DOG_JUMP_CHANCE_PER_CHASE 0.003f

#define DOG_JUMP_VEL       -8.5f



#define DRONE_ABOVE_PLAYER  80.0f

#define DRONE_Y_LERP     0.04f

#define DRONE_Y_MIN      40.0f

#define DRONE_Y_MAX      (GROUND_Y - 96 - 20)



#define DRONE_PROJ_SPEED   4.2f

#define DRONE_PROJ_LIFETIME  1400



#define SPIDER_DASH_SPEED     6.5f

#define SPIDER_DASH_DURATION_MS  280

#define SPIDER_DASH_COOLDOWN_MS  2800



#define BOSS_SPECIAL_COOLDOWN_MS 4500

#define BOSS_DASH_SPEED      9.0f

#define BOSS_DASH_DURATION_MS   320

#define BOSS_SPECIAL_PAUSE_MS   600



#define KILLS_FOR_STAGE2   8

#define KILLS_FOR_STAGE3   20



/* Level constants */

#define LEVEL1_KILL_QUOTA    15

#define LEVEL1_RESPAWN_DELAY_MS 1400



#define LVL2_HP_MULT  1.70f

#define LVL2_SPD_MULT  1.40f

#define LVL2_CD_MULT  0.60f

#define LEVEL2_RESPAWN_DELAY_MS 900



/* Level text timing */

#define LEVEL_TEXT_DURATION_MS 2000

#define LEVEL_TEXT_FADE_MS    500



/* [G2] Scroll-based spawn constants */

#define SPAWN_TRIGGER_AHEAD  400.0f  /* px ahead of worldX where enemies spawn */

#define SPAWN_TRIGGER_RANGE  300.0f  /* random spread after the trigger point */

#define SPAWN_STEP      200.0f  /* player must advance this much to allow new spawn */



/* [G4] Max simultaneous attackers */

#define MAX_SIMULTANEOUS_ATTACKERS 2



/* ═══════════════════════════════════════════════════════════════════════════

  Enums

  ═══════════════════════════════════════════════════════════════════════════ */



typedef enum { LEVEL_1 = 0, LEVEL_2 } GameLevel;



typedef enum {

  STAGE_DOGS_ONLY=0, STAGE_DOGS_SPIDERS,

  STAGE_DOGS_SPIDERS_DRONES, STAGE_BOSS, STAGE_VICTORY

} GameStage;



typedef enum {

  ANIM_ATTACK=0,ANIM_DEATH,ANIM_HURT,ANIM_WALK,ANIM_IDLE,ANIM_DODGE,ANIM_COUNT

} AnimID;

typedef enum { STATE_IDLE,STATE_PATROL,STATE_CHASE,STATE_ATTACK,STATE_HURT,STATE_DEAD } EnemyState;

typedef enum { ENEMY_DOG=0,ENEMY_DRONE=1,ENEMY_SPIDER=2,ENEMY_CYBORG=3 } EnemyType;

typedef enum { BOSS_PHASE_1=0,BOSS_PHASE_2,BOSS_PHASE_3 } BossPhase;



/* ═══════════════════════════════════════════════════════════════════════════

  Structs

  ═══════════════════════════════════════════════════════════════════════════ */



typedef struct {

  int frameW,frameH,sheetOffsetX,sheetOffsetY,paddingX,paddingY;

  int rowAttack,framesAttack,rowDeath,framesDeath,rowHurt,framesHurt;

  int rowWalk,framesWalk,rowIdle,framesIdle,rowDodge,framesDodge;

  int spfAttack,spfDeath,spfHurt,spfWalk,spfIdle,spfDodge;

} AnimConfig;



typedef struct {

  SDL_Rect frames[17];

  int count, spf;

  bool loops, valid, interruptible;

} AnimClip;



typedef struct {

  int magnitude; Uint32 startMs,durationMs; bool active;

} ScreenShake;



typedef struct {

  EnemyType type;

  bool    active,enabled;

  float   x,y,velX,velY,targetVelX;

  bool    onGround,facingRight;

  float   knockbackVelX; Uint32 knockbackEndMs;

  SDL_Rect  box;

  int    hp,maxHp;

  EnemyState state,prevState;

  float   patrolA,patrolB,detectRange,engageDistance,attackRange,speed;

  Uint32   stateTimer,lastAttackMs;

  bool    attackHit,windupDone;

  Uint32   attackStartMs;



  AnimID  currentAnim;

  AnimID  resolvedAnim;    /* ANIM_COUNT = sentinel */

  AnimClip clips[ANIM_COUNT];

  int   frame;

  Uint32  frameTimer,animStartMs;

  bool   animDone;



  SDL_Texture *texture;

  SDL_Texture *animTextures[ANIM_COUNT];

  int     texW,texH,frameW,frameH;



  AnimID  fallback[ANIM_COUNT];

  SDL_Color debugColor;



  float spawnX,spawnY;

  Uint32 deathTime;

  bool  pendingRespawn;



  BossPhase bossPhase;

  int    baseHp;

  float   baseSpeed;

  Uint32  attackCooldown;

  bool   isJumpAttacking;



  float droneTargetY;



  bool  droneProjectileActive;

  float projX,projY,projVelX,projVelY;

  Uint32 projSpawnMs;



  bool  isDashing;

  Uint32 dashStartMs,lastDashMs;

  float dashVelX;



  bool  specialActive,specialPausing;

  Uint32 lastSpecialMs,specialStartMs,specialPauseStartMs;

  int  specialHitCount;

} Enemy;



/* ═══════════════════════════════════════════════════════════════════════════

  Anim configs

  ═══════════════════════════════════════════════════════════════════════════ */



static const AnimConfig ANIM_CONFIG[4] = {

  [ENEMY_DOG]={

    .frameW=144,.frameH=96,

    .rowAttack=0,.framesAttack=17,.rowDeath=1,.framesDeath=9,

    .rowHurt=2,.framesHurt=6,.rowWalk=3,.framesWalk=12,

    .rowIdle=4,.framesIdle=4,.rowDodge=5,.framesDodge=6,

    .spfAttack=55,.spfDeath=90,.spfHurt=50,.spfWalk=80,.spfIdle=200,.spfDodge=45,

  },

  [ENEMY_DRONE]={

    .frameW=96,.frameH=96,

    .rowAttack=0,.framesAttack=15,.rowDeath=1,.framesDeath=12,

    .rowHurt=2,.framesHurt=4,.rowWalk=3,.framesWalk=5,

    .rowIdle=4,.framesIdle=6,.rowDodge=5,.framesDodge=5,

    .spfAttack=65,.spfDeath=95,.spfHurt=55,.spfWalk=100,.spfIdle=160,.spfDodge=50,

  },

  [ENEMY_SPIDER]={

    .frameW=96,.frameH=96,

    .rowAttack=0,.framesAttack=11,.rowDeath=1,.framesDeath=9,

    .rowHurt=2,.framesHurt=8,.rowWalk=3,.framesWalk=10,

    .rowIdle=4,.framesIdle=4,.rowDodge=5,.framesDodge=4,

    .spfAttack=70,.spfDeath=90,.spfHurt=60,.spfWalk=75,.spfIdle=220,.spfDodge=45,

  },

  [ENEMY_CYBORG]={

    .frameW=48,.frameH=48,

    .rowAttack=0,.framesAttack=6,.rowDeath=0,.framesDeath=6,

    .rowHurt=0,.framesHurt=3,.rowWalk=0,.framesWalk=8,

    .rowIdle=0,.framesIdle=4,.rowDodge=-1,.framesDodge=0,

    .spfAttack=60,.spfDeath=100,.spfHurt=45,.spfWalk=90,.spfIdle=180,.spfDodge=0,

  },

};



static const SDL_Color DEBUG_COLORS[4]={

  {220,80,80,255},{80,160,220,255},{80,200,80,255},{200,120,220,255}

};

static const int DEFAULT_SPF[ANIM_COUNT]={80,95,65,105,170,55};



/* ═══════════════════════════════════════════════════════════════════════════

  Math helpers

  ═══════════════════════════════════════════════════════════════════════════ */



static float lerpf(float a,float b,float t){return a+(b-a)*t;}

static float my_absf(float v){return v<0?-v:v;}

static float my_clampf(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}

static bool fvalid(float v){return !isnan(v)&&!isinf(v);}



static void triggerShake(ScreenShake *s,int mag,Uint32 dur,Uint32 now){

  if(!s||(s->active&&mag<=s->magnitude))return;

  s->magnitude=mag;s->durationMs=dur;s->startMs=now;s->active=true;

}

static int safeSPF(int v,AnimID id){

  if(v<MIN_SPF)v=DEFAULT_SPF[id];

  return v<MIN_SPF?MIN_SPF:v;

}



/* ═══════════════════════════════════════════════════════════════════════════

  [G1] Visual-ground helpers — each uses its render scale so the visual

  bottom pixel lands exactly on GROUND_Y, matching how renderEnemy draws them.

  ═══════════════════════════════════════════════════════════════════════════ */

static float cyborgGroundY(void){

  return (float)(GROUND_Y -

      (int)((float)ANIM_CONFIG[ENEMY_CYBORG].frameH * CYBORG_RENDER_SCALE));

}

static float dogGroundY(void){

  return (float)(GROUND_Y -

      (int)((float)ANIM_CONFIG[ENEMY_DOG].frameH * DOG_RENDER_SCALE));

}

static float spiderGroundY(void){

  return (float)(GROUND_Y -

      (int)((float)ANIM_CONFIG[ENEMY_SPIDER].frameH * SPIDER_RENDER_SCALE));

}



/* Returns the correct visual-ground Y for any ground enemy type. */

static float groundYForType(EnemyType t){

  switch(t){

    case ENEMY_DOG:  return dogGroundY();

    case ENEMY_SPIDER: return spiderGroundY();

    case ENEMY_CYBORG: return cyborgGroundY();

    default:      return (float)(GROUND_Y - ANIM_CONFIG[t].frameH);

  }

}



/* ═══════════════════════════════════════════════════════════════════════════

  Config accessors

  ═══════════════════════════════════════════════════════════════════════════ */



static int cfg_row(const AnimConfig *c,AnimID id){

  switch(id){

    case ANIM_ATTACK:return c->rowAttack; case ANIM_DEATH:return c->rowDeath;

    case ANIM_HURT: return c->rowHurt;  case ANIM_WALK: return c->rowWalk;

    case ANIM_IDLE: return c->rowIdle;  case ANIM_DODGE:return c->rowDodge;

    default:return -1;

  }

}

static int cfg_count(const AnimConfig *c,AnimID id){

  switch(id){

    case ANIM_ATTACK:return c->framesAttack; case ANIM_DEATH:return c->framesDeath;

    case ANIM_HURT: return c->framesHurt;  case ANIM_WALK: return c->framesWalk;

    case ANIM_IDLE: return c->framesIdle;  case ANIM_DODGE:return c->framesDodge;

    default:return 0;

  }

}

static int cfg_spf(const AnimConfig *c,AnimID id){

  int v=0;

  switch(id){

    case ANIM_ATTACK:v=c->spfAttack;break; case ANIM_DEATH:v=c->spfDeath;break;

    case ANIM_HURT: v=c->spfHurt; break; case ANIM_WALK: v=c->spfWalk; break;

    case ANIM_IDLE: v=c->spfIdle; break; case ANIM_DODGE:v=c->spfDodge;break;

    default:break;

  }

  return safeSPF(v,id);

}



/* ═══════════════════════════════════════════════════════════════════════════

  Frame validation

  ═══════════════════════════════════════════════════════════════════════════ */



static bool validateRect(const SDL_Rect *r,int tw,int th){

  if(!r||r->w<=0||r->h<=0||r->x<0||r->y<0) return false;

  if(tw<=0||th<=0) return false;

  if(r->x+r->w>tw||r->y+r->h>th) return false;

  return true;

}

static int clampFrame(int f,int n){

  if(n<=0) return -1;

  if(f<0) return 0;

  if(f>=n) return n-1;

  return f;

}



/* ═══════════════════════════════════════════════════════════════════════════

  Clip building

  ═══════════════════════════════════════════════════════════════════════════ */



static void buildClipsFromConfig(Enemy *e,const AnimConfig *cfg){

  for(int a=0;a<ANIM_COUNT;a++){

    AnimClip *c=&e->clips[a];

    memset(c,0,sizeof(*c));

    int row=cfg_row(cfg,(AnimID)a);

    int cnt=cfg_count(cfg,(AnimID)a);

    c->spf=cfg_spf(cfg,(AnimID)a);

    c->loops=(a!=ANIM_ATTACK&&a!=ANIM_DEATH&&a!=ANIM_HURT);

    c->interruptible=(a==ANIM_IDLE||a==ANIM_WALK||a==ANIM_DODGE);

    if(row<0||cnt<=0){c->valid=false;continue;}

    int maxF=(int)(sizeof(c->frames)/sizeof(c->frames[0]));

    if(cnt>maxF)cnt=maxF;

    c->count=0;

    for(int f=0;f<cnt;f++){

      SDL_Rect r={

        cfg->sheetOffsetX+f*(cfg->frameW+cfg->paddingX),

        cfg->sheetOffsetY+row*(cfg->frameH+cfg->paddingY),

        cfg->frameW,cfg->frameH

      };

      if(!validateRect(&r,e->texW,e->texH)) break;

      c->frames[c->count++]=r;

    }

    c->valid=(c->count>0);

  }

}



static void rebuildCyborgClip(Enemy *e,AnimID id){

  AnimClip *c=&e->clips[id];

  memset(c,0,sizeof(*c));

  SDL_Texture *tex=e->animTextures[id];

  if(!tex){c->valid=false;return;}

  int tw=0,th=0;

  SDL_QueryTexture(tex,NULL,NULL,&tw,&th);

  if(tw<=0||th<=0){c->valid=false;return;}

  const AnimConfig *cfg=&ANIM_CONFIG[ENEMY_CYBORG];

  if(cfg->frameW<=0){c->valid=false;return;}

  int actual=tw/cfg->frameW;

  int want=cfg_count(cfg,id);

  int cnt=(want>0&&actual>=want)?want:actual;

  int maxF=(int)(sizeof(c->frames)/sizeof(c->frames[0]));

  if(cnt>maxF)cnt=maxF;

  c->spf=cfg_spf(cfg,id);

  c->loops=(id!=ANIM_ATTACK&&id!=ANIM_DEATH&&id!=ANIM_HURT);

  c->interruptible=(id==ANIM_IDLE||id==ANIM_WALK);

  c->count=0;

  for(int f=0;f<cnt;f++){

    SDL_Rect r={f*cfg->frameW,0,cfg->frameW,cfg->frameH};

    if(!validateRect(&r,tw,th)) break;

    c->frames[c->count++]=r;

  }

  c->valid=(c->count>0);

}



/* ═══════════════════════════════════════════════════════════════════════════

  Fallback table

  ═══════════════════════════════════════════════════════════════════════════ */



static AnimID findAnyValidClip(const Enemy *e){

  static const AnimID p[]={ANIM_WALK,ANIM_IDLE,ANIM_ATTACK,ANIM_HURT,ANIM_DEATH,ANIM_DODGE};

  for(int i=0;i<(int)(sizeof(p)/sizeof(p[0]));i++)

    if(e->clips[p[i]].valid&&e->clips[p[i]].count>0) return p[i];

  return ANIM_WALK;

}

static AnimID pickValid(const Enemy *e,AnimID a,AnimID b,AnimID c_){

  if(e->clips[a].valid&&e->clips[a].count>0) return a;

  if(e->clips[b].valid&&e->clips[b].count>0) return b;

  if(e->clips[c_].valid&&e->clips[c_].count>0) return c_;

  return findAnyValidClip(e);

}

static void buildFallbackTable(Enemy *e){

  for(int a=0;a<ANIM_COUNT;a++){

    AnimID ch;

    if(e->clips[a].valid&&e->clips[a].count>0){ch=(AnimID)a;}

    else switch((AnimID)a){

      case ANIM_IDLE: ch=pickValid(e,ANIM_WALK,ANIM_ATTACK,ANIM_WALK);  break;

      case ANIM_DODGE: ch=pickValid(e,ANIM_WALK,ANIM_ATTACK,ANIM_WALK);  break;

      case ANIM_HURT: ch=pickValid(e,ANIM_IDLE,ANIM_WALK,ANIM_WALK);   break;

      case ANIM_ATTACK:ch=pickValid(e,ANIM_WALK,ANIM_IDLE,ANIM_WALK);   break;

      case ANIM_DEATH: ch=pickValid(e,ANIM_HURT,ANIM_IDLE,ANIM_WALK);   break;

      case ANIM_WALK: ch=pickValid(e,ANIM_IDLE,ANIM_ATTACK,ANIM_ATTACK); break;

      default:     ch=findAnyValidClip(e); break;

    }

    if(!(e->clips[ch].valid&&e->clips[ch].count>0)) ch=findAnyValidClip(e);

    e->fallback[a]=ch;

  }

}



/* ═══════════════════════════════════════════════════════════════════════════

  Texture load

  ═══════════════════════════════════════════════════════════════════════════ */



static SDL_Texture *loadTexture(SDL_Renderer *r,const char *p){

  SDL_Surface *s=IMG_Load(p);

  if(!s){SDL_Log("[WARN] IMG_Load '%s': %s",p,IMG_GetError());return NULL;}

  SDL_Texture *t=SDL_CreateTextureFromSurface(r,s);

  SDL_FreeSurface(s);

  return t;

}



/* ═══════════════════════════════════════════════════════════════════════════

  Enemy init

  [G1] All ground enemies now snap to their visual-ground Y at init.

  ═══════════════════════════════════════════════════════════════════════════ */



static void initEnemyBase(Enemy *e,EnemyType type,float x,float y,SDL_Texture *tex){

  memset(e,0,sizeof(*e));

  e->type=type; e->active=true; e->enabled=false;

  e->texture=tex; e->debugColor=DEBUG_COLORS[type];

  if(tex) SDL_QueryTexture(tex,NULL,NULL,&e->texW,&e->texH);

  const AnimConfig *cfg=&ANIM_CONFIG[type];

  e->frameW=cfg->frameW; e->frameH=cfg->frameH;



  switch(type){

    case ENEMY_DOG:

      e->speed=SPEED_DOG; e->detectRange=DETECT_DOG;

      e->engageDistance=ENGAGE_DOG; e->attackRange=ATTACK_DOG;

      e->hp=e->maxHp=e->baseHp=60; e->baseSpeed=SPEED_DOG;

      e->attackCooldown=ATTACK_COOLDOWN_MS;

      e->box=(SDL_Rect){14,30,100,60};

      /* [G1] snap visual bottom to ground */

      e->y=e->spawnY=dogGroundY();

      e->onGround=true; e->velY=0.0f;

      break;

    case ENEMY_DRONE:

      e->speed=SPEED_DRONE; e->detectRange=DETECT_DRONE;

      e->engageDistance=ENGAGE_DRONE; e->attackRange=ATTACK_DRONE;

      e->hp=e->maxHp=e->baseHp=40; e->baseSpeed=SPEED_DRONE;

      e->attackCooldown=ATTACK_COOLDOWN_MS;

      e->box=(SDL_Rect){18,18,60,60};

      e->y=e->spawnY=y;      /* drone: use caller-supplied Y */

      e->droneTargetY=y;

      e->onGround=false;

      break;

    case ENEMY_SPIDER:

      e->speed=SPEED_SPIDER; e->detectRange=DETECT_SPIDER;

      e->engageDistance=ENGAGE_SPIDER; e->attackRange=ATTACK_SPIDER;

      e->hp=e->maxHp=e->baseHp=50; e->baseSpeed=SPEED_SPIDER;

      e->attackCooldown=ATTACK_COOLDOWN_MS;

      e->box=(SDL_Rect){10,24,76,68};

      /* [G1] snap visual bottom to ground */

      e->y=e->spawnY=spiderGroundY();

      e->onGround=true; e->velY=0.0f;

      break;

    case ENEMY_CYBORG:

      e->speed=SPEED_CYBORG; e->detectRange=DETECT_CYBORG;

      e->engageDistance=ENGAGE_CYBORG; e->attackRange=ATTACK_CYBORG;

      e->hp=e->maxHp=e->baseHp=600; e->baseSpeed=SPEED_CYBORG;

      e->attackCooldown=ATTACK_COOLDOWN_MS;

      e->box=(SDL_Rect){8,4,32,42};

      /* [G1] visual-scale snap */

      e->y=e->spawnY=cyborgGroundY();

      e->onGround=true; e->velY=0.0f;

      break;

  }



  e->x=e->spawnX=x;

  e->patrolA=x-PATROL_HALF; e->patrolB=x+PATROL_HALF;

  e->state=STATE_IDLE; e->prevState=STATE_IDLE;

  e->stateTimer=SDL_GetTicks(); e->facingRight=true;

  e->bossPhase=BOSS_PHASE_1;



  Uint32 now=SDL_GetTicks();

  e->currentAnim=ANIM_IDLE;

  e->resolvedAnim=ANIM_COUNT; /* sentinel */

  e->frame=0; e->frameTimer=now; e->animStartMs=now; e->animDone=false;



  buildClipsFromConfig(e,cfg);

  buildFallbackTable(e);

}



static void initCyborgAnimTextures(Enemy *e,

  SDL_Texture *tI,SDL_Texture *tW,SDL_Texture *tA,SDL_Texture *tH,SDL_Texture *tD)

{

  e->animTextures[ANIM_IDLE]=tI; e->animTextures[ANIM_WALK]=tW;

  e->animTextures[ANIM_ATTACK]=tA; e->animTextures[ANIM_HURT]=tH;

  e->animTextures[ANIM_DEATH]=tD; e->animTextures[ANIM_DODGE]=NULL;

  rebuildCyborgClip(e,ANIM_IDLE); rebuildCyborgClip(e,ANIM_WALK);

  rebuildCyborgClip(e,ANIM_ATTACK); rebuildCyborgClip(e,ANIM_HURT);

  rebuildCyborgClip(e,ANIM_DEATH);

  buildFallbackTable(e);

  SDL_Texture *first=tI?tI:tW?tW:tA?tA:tH?tH:tD;

  if(first&&!e->texture){e->texture=first;SDL_QueryTexture(first,NULL,NULL,&e->texW,&e->texH);}

}



/* ═══════════════════════════════════════════════════════════════════════════

  Animation control — v4 logic preserved verbatim, DO NOT MODIFY

  ═══════════════════════════════════════════════════════════════════════════ */



static void forceAnimReset(Enemy *e,AnimID anim,Uint32 now){

  if(anim>=ANIM_COUNT) anim=ANIM_IDLE;

  e->currentAnim=anim;

  e->resolvedAnim=ANIM_COUNT; /* sentinel */

  e->frame=0; e->frameTimer=now; e->animStartMs=now; e->animDone=false;

}



static void setState(Enemy *e,EnemyState ns,Uint32 now){

  if(e->state==ns||e->state==STATE_DEAD) return;

  bool urgent=(ns==STATE_HURT||ns==STATE_DEAD);

  if(!urgent&&(now-e->stateTimer)<(Uint32)STATE_MIN_MS) return;

  if(!urgent){

    AnimID res=(e->resolvedAnim<ANIM_COUNT)?e->resolvedAnim:e->fallback[e->currentAnim];

    if(res<ANIM_COUNT){

      const AnimClip *cur=&e->clips[res];

      if(!cur->interruptible&&!e->animDone&&

        (now-e->animStartMs)<(Uint32)ANIM_MIN_INTERRUPTIBLE_MS) return;

    }

  }

  e->prevState=e->state; e->state=ns; e->stateTimer=now;

  AnimID raw;

  switch(ns){

    case STATE_IDLE:  raw=ANIM_IDLE;  break;

    case STATE_PATROL: raw=ANIM_WALK;  break;

    case STATE_CHASE: raw=ANIM_WALK;  break;

    case STATE_ATTACK: raw=ANIM_ATTACK; break;

    case STATE_HURT:  raw=ANIM_HURT;  break;

    case STATE_DEAD:  raw=ANIM_DEATH; break;

    default:      raw=ANIM_IDLE;  break;

  }

  forceAnimReset(e,raw,now);

}



static void updateAnimation(Enemy *e,Uint32 now){

  if(e->currentAnim>=ANIM_COUNT) e->currentAnim=ANIM_IDLE;



  AnimID res=e->fallback[e->currentAnim];

  if(res>=ANIM_COUNT||!e->clips[res].valid||e->clips[res].count<=0)

    res=findAnyValidClip(e);



  bool clipSwitched=(res!=e->resolvedAnim);

  e->resolvedAnim=res; /* MUST be before any early return */



  if(res>=ANIM_COUNT||!e->clips[res].valid||e->clips[res].count<=0) return;



  AnimClip *c=&e->clips[res];

  if(clipSwitched){

    e->frame=0; e->frameTimer=now; e->animDone=false; e->animStartMs=now;

  }

  if(c->count<=0){e->frame=0;return;}



  int safe=clampFrame(e->frame,c->count);

  if(safe<0){e->frame=0;return;}

  e->frame=safe;



  int spf=safeSPF(c->spf,e->currentAnim);

  if((now-e->frameTimer)<(Uint32)spf) return;

  e->frameTimer=now;

  e->frame++;

  if(e->frame>=c->count){

    if(c->loops) e->frame=0;

    else{e->frame=c->count-1;e->animDone=true;}

  }

  safe=clampFrame(e->frame,c->count);

  e->frame=(safe>=0)?safe:0;

}



/* ═══════════════════════════════════════════════════════════════════════════

  Misc helpers

  ═══════════════════════════════════════════════════════════════════════════ */



static float distanceTo(const Enemy *e,float px,float py){

  float cx=e->x+e->frameW*0.5f,cy=e->y+e->frameH*0.5f;

  float dx=px-cx,dy=py-cy;

  return sqrtf(dx*dx+dy*dy);

}

static void applyScaling(Enemy *e,float hM,float sM,float cM){

  if(e->type==ENEMY_CYBORG) return;

  e->maxHp=(int)((float)e->baseHp*hM); e->hp=e->maxHp;

  e->speed=e->baseSpeed*sM;

  e->attackCooldown=(Uint32)((float)ATTACK_COOLDOWN_MS*my_clampf(cM,0.5f,1.0f));

}



/*

 * activateEnemy — [G1] All ground enemies snap to visual-ground Y.

 *  spawnX can be overridden by the caller before this is called (for scroll spawns).

 */

static void activateEnemy(Enemy *e,Uint32 now,float hM,float sM,float cM){

  e->enabled=true;

  e->x=e->spawnX;

  /* [G1] snap Y to visual ground for all ground types */

  if(e->type==ENEMY_DRONE)

    e->y=e->spawnY;

  else{

    e->y=groundYForType(e->type);

    e->velY=0.0f;

    e->onGround=true;

  }

  e->velX=e->targetVelX=e->knockbackVelX=0.0f;

  e->knockbackEndMs=0; e->state=STATE_PATROL; e->prevState=STATE_PATROL;

  e->frame=0; e->animDone=false; e->attackHit=false; e->windupDone=false;

  e->pendingRespawn=false; e->stateTimer=e->frameTimer=e->animStartMs=now;

  e->lastAttackMs=now; e->currentAnim=ANIM_WALK;

  e->resolvedAnim=ANIM_COUNT; /* sentinel */

  e->bossPhase=BOSS_PHASE_1; e->isDashing=false;

  e->droneProjectileActive=false;

  e->projX=e->projY=e->projVelX=e->projVelY=0.0f;

  e->projSpawnMs=0;

  e->specialActive=e->specialPausing=e->isJumpAttacking=false;

  e->onGround=(e->type!=ENEMY_DRONE);

  e->facingRight=true; e->lastDashMs=e->lastSpecialMs=0;

  if(e->type==ENEMY_DRONE){e->droneTargetY=e->spawnY;e->velY=0.0f;}

  applyScaling(e,hM,sM,cM);

}



static void deactivateEnemy(Enemy *e){

  e->enabled=false; e->state=STATE_DEAD; e->animDone=true;

  e->velX=e->velY=e->targetVelX=0.0f; e->pendingRespawn=false;

}

void hurtEnemy(Enemy *e,int dmg,float srcX,ScreenShake *shake){

  if(!e->active||!e->enabled||e->state==STATE_DEAD) return;

  Uint32 now=SDL_GetTicks();

  e->hp-=dmg;

  if(e->hp<=0){

    e->hp=0;setState(e,STATE_DEAD,now);

    if(e->type==ENEMY_CYBORG)SDL_Log("=== CYBORG DEFEATED ===");

  } else setState(e,STATE_HURT,now);

  float dir=(e->x+e->frameW*0.5f>srcX)?1.0f:-1.0f;

  e->knockbackVelX=dir*KNOCKBACK_SPEED_ENEMY;

  e->knockbackEndMs=now+KNOCKBACK_DURATION_MS;

  if(shake) triggerShake(shake,

    (e->type==ENEMY_CYBORG)?SHAKE_MAGNITUDE_BOSS:SHAKE_MAGNITUDE_HIT,

    SHAKE_DURATION_MS,now);

}

bool enemyAttackHitPlayer(const Enemy *e,float px,float py,int pw,int ph){

  if(!e->enabled||e->state!=STATE_ATTACK||e->attackHit||!e->windupDone) return false;

  SDL_Rect eB={(int)(e->x+e->box.x),(int)(e->y+e->box.y),e->box.w,e->box.h};

  SDL_Rect pB={(int)px,(int)py,pw,ph};

  SDL_Rect tmp;

  return SDL_IntersectRect(&eB,&pB,&tmp);

}



/* ═══════════════════════════════════════════════════════════════════════════

  Boss phase

  ═══════════════════════════════════════════════════════════════════════════ */



static void updateBossPhase(Enemy *e){

  if(e->type!=ENEMY_CYBORG||e->state==STATE_DEAD) return;

  float pct=(e->maxHp>0)?(float)e->hp/(float)e->maxHp:0.0f;

  BossPhase np=(pct>0.60f)?BOSS_PHASE_1:(pct>0.30f)?BOSS_PHASE_2:BOSS_PHASE_3;

  if(np==e->bossPhase) return;

  e->bossPhase=np;

  switch(np){

    case BOSS_PHASE_1:

      e->speed=SPEED_CYBORG;e->attackCooldown=ATTACK_COOLDOWN_MS;

      e->detectRange=DETECT_CYBORG;SDL_Log("[BOSS] P1");break;

    case BOSS_PHASE_2:

      e->speed=SPEED_CYBORG*1.30f;

      e->attackCooldown=(Uint32)(ATTACK_COOLDOWN_MS*0.70f);

      e->detectRange=DETECT_CYBORG*1.20f;SDL_Log("[BOSS] P2");break;

    case BOSS_PHASE_3:

      e->speed=SPEED_CYBORG*1.60f;

      e->attackCooldown=(Uint32)(ATTACK_COOLDOWN_MS*0.50f);

      e->detectRange=DETECT_CYBORG*1.40f;

      for(int a=0;a<ANIM_COUNT;a++)

        if(e->clips[a].valid){

          int ns=e->clips[a].spf/2;

          e->clips[a].spf=ns>=MIN_SPF?ns:MIN_SPF;

        }

      SDL_Log("[BOSS] P3 BERSERK");break;

  }

}



/* ═══════════════════════════════════════════════════════════════════════════

  Type-specific AI (unchanged from v6)

  ═══════════════════════════════════════════════════════════════════════════ */



static void updateDogAI(Enemy *e,float px,Uint32 now){

  if(e->state==STATE_CHASE&&e->onGround){

    float roll=(float)rand()/(float)RAND_MAX;

    if(roll<DOG_JUMP_CHANCE_PER_CHASE){

      float dir=(px>e->x+e->frameW*0.5f)?1.0f:-1.0f;

      e->velY=DOG_JUMP_VEL; e->velX=dir*e->speed*2.2f;

      e->onGround=false; e->isJumpAttacking=true;

      setState(e,STATE_ATTACK,now);

    }

  }

  if(e->isJumpAttacking&&e->onGround&&e->state==STATE_ATTACK)

    e->isJumpAttacking=false;

}

static void updateDroneTargetY(Enemy *e,float py){

  float t=py-DRONE_ABOVE_PLAYER;

  e->droneTargetY=my_clampf(t,DRONE_Y_MIN,(float)DRONE_Y_MAX);

}

static void startSpiderDash(Enemy *e,float px,Uint32 now){

  float dir=(px>e->x+e->frameW*0.5f)?1.0f:-1.0f;

  e->dashVelX=dir*SPIDER_DASH_SPEED; e->isDashing=true;

  e->dashStartMs=now; e->facingRight=(dir>0.0f);

  setState(e,STATE_ATTACK,now);

}

static void updateSpiderDash(Enemy *e,Uint32 now){

  if(!e->isDashing) return;

  if((now-e->dashStartMs)>=SPIDER_DASH_DURATION_MS){

    e->isDashing=false;e->dashVelX=0.0f;e->lastDashMs=now;e->animDone=true;

  } else e->targetVelX=e->dashVelX;

}

static void startBossSpecial(Enemy *e,float px,Uint32 now){

  e->specialPausing=true; e->specialPauseStartMs=now;

  e->specialHitCount=0; e->targetVelX=0.0f;

  float dir=(px>e->x+e->frameW*0.5f)?1.0f:-1.0f;

  e->dashVelX=dir*BOSS_DASH_SPEED; e->facingRight=(dir>0.0f);

}

static void updateBossSpecial(Enemy *e,float px,float py,Uint32 now,ScreenShake *shake){

  (void)py;

  if(!e->specialActive&&!e->specialPausing) return;

  if(e->specialPausing){

    e->targetVelX=0.0f;

    if((now-e->specialPauseStartMs)>=(Uint32)BOSS_SPECIAL_PAUSE_MS){

      e->specialPausing=false; e->specialActive=true;

      e->specialStartMs=now; setState(e,STATE_ATTACK,now);

    }

    return;

  }

  Uint32 el=now-e->specialStartMs;

  if(el<(Uint32)BOSS_DASH_DURATION_MS){

    e->targetVelX=e->dashVelX;

    if(e->specialHitCount<2){

      float dist=distanceTo(e,px,py);

      if(dist<=e->attackRange*1.5f){

        e->specialHitCount++; e->attackHit=false;

        if(shake)triggerShake(shake,SHAKE_MAGNITUDE_BOSS,SHAKE_DURATION_MS,now);

      }

    }

  } else {

    e->specialActive=false; e->dashVelX=0.0f;

    e->lastSpecialMs=now; e->animDone=true; e->targetVelX=0.0f;

  }

}



/* ═══════════════════════════════════════════════════════════════════════════

  Main AI — [G3] + [G4] modifications

  updateAI now takes bool canAttack (from mass-aggro limiter in EM_update).

  Detection is 3-zone: patrol → chase → attack (only within engageDistance).

  ═══════════════════════════════════════════════════════════════════════════ */



static void updateAI(Enemy *e,float px,float py,Uint32 now,ScreenShake *shake,bool canAttack){

  if(e->state==STATE_DEAD) return;

  if(e->type==ENEMY_CYBORG) updateBossPhase(e);

  if(e->type==ENEMY_DRONE) updateDroneTargetY(e,py);



  if(e->state==STATE_HURT){

    if(e->animDone||(now-e->stateTimer)>(Uint32)HURT_DURATION_MS){

      EnemyState rec=(e->prevState==STATE_DEAD||e->prevState==STATE_HURT)

              ?STATE_PATROL:e->prevState;

      setState(e,rec,now);

    }

    e->targetVelX=0.0f; return;

  }



  if(e->state==STATE_ATTACK){

    if(e->type==ENEMY_CYBORG&&(e->specialActive||e->specialPausing)){

      updateBossSpecial(e,px,py,now,shake); return;

    }

    if(e->type==ENEMY_SPIDER&&e->isDashing) updateSpiderDash(e,now);

    else e->targetVelX=0.0f;



    if(e->type==ENEMY_DRONE){

      if(!e->droneProjectileActive){

        e->droneProjectileActive=true;

        e->projSpawnMs=now;

        float dx=e->x+e->frameW*0.5f,dy=e->y+e->frameH*0.5f;

        e->projX=dx; e->projY=dy;

        float vx=px-dx,vy=py-dy;

        float len=sqrtf(vx*vx+vy*vy);

        if(len<1.0f)len=1.0f;

        e->projVelX=(vx/len)*DRONE_PROJ_SPEED;

        e->projVelY=(vy/len)*DRONE_PROJ_SPEED;

        e->currentAnim=ANIM_IDLE;

        forceAnimReset(e,ANIM_IDLE,now);

        e->animDone=true;

      }

      e->projX+=e->projVelX;

      e->projY+=e->projVelY;

      if((now-e->projSpawnMs)>(Uint32)DRONE_PROJ_LIFETIME)

        e->droneProjectileActive=false;

    }



    if(!e->windupDone&&(now-e->attackStartMs)>=(Uint32)ATTACK_WINDUP_MS)

      e->windupDone=true;

    if(e->windupDone&&!e->attackHit) e->attackHit=true;



    if(e->animDone){

      if(e->type!=ENEMY_DRONE){

        AnimID ra=e->fallback[ANIM_ATTACK];

        if(e->clips[ra].valid&&e->clips[ra].count>0){

          int spf=safeSPF(e->clips[ra].spf,ANIM_ATTACK);

          if((now-e->frameTimer)>=(Uint32)spf){

            e->attackHit=false; e->windupDone=false;

            e->lastAttackMs=now; e->isDashing=false;

            float dist=distanceTo(e,px,py);

            setState(e,dist<e->detectRange?STATE_CHASE:STATE_PATROL,now);

          }

        }

      } else {

        if(!e->droneProjectileActive){

          e->attackHit=false; e->windupDone=false;

          e->lastAttackMs=now; e->animDone=true;

          float dist=distanceTo(e,px,py);

          setState(e,dist<e->detectRange?STATE_CHASE:STATE_PATROL,now);

        }

      }

    }

    return;

  }



  /* Dog jump-attack can override before distance check */

  if(e->type==ENEMY_DOG) updateDogAI(e,px,now);



  float dist=distanceTo(e,px,py);



  /* ── [G3] 3-zone detection ─────────────────────────────────────────────

   * Zone 1 (> detectRange)    : patrol only — ignore player

   * Zone 2 (detectRange..engage) : chase — follow but do NOT attack

   * Zone 3 (<= engageDistance)  : attack if cooldown met AND canAttack ([G4])

   * ─────────────────────────────────────────────────────────────────────*/



  if(dist<=e->engageDistance&&canAttack&&

    (now-e->lastAttackMs)>=e->attackCooldown)

  {

    /* [G4] canAttack=false means the global cap is reached; stay in chase */

    if(e->type==ENEMY_CYBORG&&e->bossPhase==BOSS_PHASE_3&&

      (now-e->lastSpecialMs)>=(Uint32)BOSS_SPECIAL_COOLDOWN_MS){

      startBossSpecial(e,px,now); return;

    }

    if(e->type==ENEMY_SPIDER&&(now-e->lastDashMs)>=(Uint32)SPIDER_DASH_COOLDOWN_MS){

      startSpiderDash(e,px,now); return;

    }

    AnimID ra=e->fallback[ANIM_ATTACK];

    SDL_Texture *at=e->animTextures[ra]?e->animTextures[ra]:e->texture;

    if(at&&e->clips[ra].valid&&e->clips[ra].count>0){

      e->attackStartMs=now; e->windupDone=false; e->attackHit=false;

      setState(e,STATE_ATTACK,now); return;

    }

  }



  /* Zone transitions */

  if(dist<=e->detectRange) setState(e,STATE_CHASE,now);

  else if(e->state==STATE_CHASE) setState(e,STATE_PATROL,now);



  switch(e->state){

    case STATE_IDLE:

      e->targetVelX=0.0f;

      if((now-e->stateTimer)>1200) setState(e,STATE_PATROL,now);

      break;

    case STATE_PATROL:

      if(e->x<=e->patrolA) e->facingRight=true;

      else if((e->x+e->frameW)>=e->patrolB) e->facingRight=false;

      e->targetVelX=e->facingRight?e->speed:-e->speed;

      break;

    case STATE_CHASE:{

      float cx=e->x+e->frameW*0.5f;

      /* [G3] Chase speed is reduced when player is detected but not in engage zone

        so it feels like the enemy is stalking rather than rushing */

      float boost;

      if(dist<=e->engageDistance)     boost=1.35f; /* very close: normal chase */

      else if(dist<=e->detectRange*0.60f) boost=1.00f; /* mid: normal speed */

      else                 boost=0.70f; /* far detect: slow stalk */

      if(e->type==ENEMY_CYBORG) boost*=1.40f; /* boss always faster */

      if(px>cx){e->targetVelX=e->speed*boost;e->facingRight=true;}

      else{e->targetVelX=-e->speed*boost;e->facingRight=false;}

      break;

    }

    default:break;

  }

}



/* ═══════════════════════════════════════════════════════════════════════════

  Physics

  [G1] Ground snap uses groundYForType() for all non-drone enemies.

  ═══════════════════════════════════════════════════════════════════════════ */



static void updatePhysics(Enemy *e,float deltaMs,Uint32 now){

  if(e->state==STATE_DEAD) return;



  float kbVel=0.0f;

  if(now<e->knockbackEndMs){

    float t=1.0f-(float)(e->knockbackEndMs-now)/(float)KNOCKBACK_DURATION_MS;

    kbVel=e->knockbackVelX*(1.0f-t);

  } else e->knockbackVelX=0.0f;



  float nvx=lerpf(e->velX,e->targetVelX,LERP_FACTOR)+kbVel;

  e->velX=fvalid(nvx)?nvx:0.0f;



  if(e->type==ENEMY_DRONE){

    float ls=my_clampf(DRONE_Y_LERP*(deltaMs/16.0f),0.0f,1.0f);

    float ny=lerpf(e->y,e->droneTargetY,ls);

    if(!fvalid(ny)) ny=e->spawnY;

    e->y=my_clampf(ny,DRONE_Y_MIN,(float)DRONE_Y_MAX);

    e->velY=0.0f;

  } else {

    /* [G1] visual-bottom ground target for every ground enemy type */

    float gnd=groundYForType(e->type);



    if(!e->onGround){

      float nvy=e->velY+GRAVITY*(deltaMs/16.0f);

      e->velY=fvalid(nvy)?nvy:0.0f;

    }

    float ny=e->y+e->velY*(deltaMs/16.0f);

    if(!fvalid(ny)) ny=gnd;

    e->y=ny;

    if(e->y>=gnd){e->y=gnd;e->velY=0.0f;e->onGround=true;}

    else e->onGround=false;

    if(e->y<-600.0f||e->y>(float)(GROUND_Y+300)){

      e->y=gnd;e->velY=0.0f;e->onGround=true;

    }

  }



  float nx=e->x+e->velX*(deltaMs/16.0f);

  if(!fvalid(nx)) nx=e->spawnX;

  e->x=nx;

  if(e->x<0.0f){e->x=0.0f;e->velX=0.0f;}

  if(e->x>(float)(WINDOW_W-e->frameW)){e->x=(float)(WINDOW_W-e->frameW);e->velX=0.0f;}

}



/* updateEnemy now accepts canAttack for [G4] forwarding */

static void updateEnemy(Enemy *e,float px,float py,Uint32 now,float dt,

             ScreenShake *shake,bool canAttack){

  if(!e->active||!e->enabled) return;

  if(e->state==STATE_DEAD&&e->animDone) return;

  updateAI(e,px,py,now,shake,canAttack);

  updatePhysics(e,dt,now);

  updateAnimation(e,now);

}



/* ═══════════════════════════════════════════════════════════════════════════

  Render — enemy sprites (unchanged from v6, DO NOT MODIFY)

  ═══════════════════════════════════════════════════════════════════════════ */



static void dbgRect(SDL_Renderer *r,const SDL_Rect *d,SDL_Color c){

  SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a);SDL_RenderFillRect(r,d);

  SDL_SetRenderDrawColor(r,255,255,255,180);SDL_RenderDrawRect(r,d);

  SDL_RenderDrawLine(r,d->x,d->y,d->x+d->w,d->y+d->h);

  SDL_RenderDrawLine(r,d->x+d->w,d->y,d->x,d->y+d->h);

}



static void renderEnemy(Enemy *e,SDL_Renderer *ren,int sx,int sy){

  if(!e->active||!e->enabled) return;



  float rs;

  switch(e->type){

    case ENEMY_DOG:  rs=DOG_RENDER_SCALE;  break;

    case ENEMY_SPIDER: rs=SPIDER_RENDER_SCALE; break;

    case ENEMY_DRONE: rs=DRONE_RENDER_SCALE; break;

    case ENEMY_CYBORG: rs=CYBORG_RENDER_SCALE; break;

    default:      rs=1.0f;        break;

  }

  int sw=(int)((float)e->frameW*rs); if(sw<1)sw=1;

  int sh=(int)((float)e->frameH*rs); if(sh<1)sh=1;

  int ox=(sw-e->frameW)/2, oy=(sh-e->frameH)/2;

  SDL_Rect dst={(int)e->x-ox+sx,(int)e->y-oy+sy,sw,sh};



  AnimID res=e->resolvedAnim;

  if(res>=ANIM_COUNT||!e->clips[res].valid||e->clips[res].count<=0){

    if(e->currentAnim<ANIM_COUNT) res=e->fallback[e->currentAnim];

    if(res>=ANIM_COUNT||!e->clips[res].valid||e->clips[res].count<=0)

      res=findAnyValidClip(e);

  }

  if(res>=ANIM_COUNT||!e->clips[res].valid||e->clips[res].count<=0){

    dbgRect(ren,&dst,e->debugColor); goto render_projectile;

  }

  {

    AnimClip *c=&e->clips[res];

    SDL_Texture *tex=(res<ANIM_COUNT&&e->animTextures[res])

             ?e->animTextures[res]:e->texture;

    if(!tex){dbgRect(ren,&dst,e->debugColor);goto render_projectile;}

    int tw=0,th=0;

    SDL_QueryTexture(tex,NULL,NULL,&tw,&th);

    if(tw<=0||th<=0){dbgRect(ren,&dst,e->debugColor);goto render_projectile;}

    if(c->count<=0){dbgRect(ren,&dst,e->debugColor);goto render_projectile;}

    int fi=clampFrame(e->frame,c->count);

    if(fi<0){dbgRect(ren,&dst,e->debugColor);goto render_projectile;}

    SDL_Rect src=c->frames[fi];

    if(!validateRect(&src,tw,th)){

      if(c->count>0&&validateRect(&c->frames[0],tw,th)){src=c->frames[0];fi=0;}

      else{dbgRect(ren,&dst,e->debugColor);goto render_projectile;}

    }

    if(e->state==STATE_HURT){

      SDL_SetTextureColorMod(tex,255,60,60);

    } else if(e->type==ENEMY_CYBORG){

      switch(e->bossPhase){

        case BOSS_PHASE_1:SDL_SetTextureColorMod(tex,255,255,255);break;

        case BOSS_PHASE_2:SDL_SetTextureColorMod(tex,255,200,80); break;

        case BOSS_PHASE_3:SDL_SetTextureColorMod(tex,255,60,60); break;

      }

    } else SDL_SetTextureColorMod(tex,255,255,255);

    SDL_RendererFlip flip=e->facingRight?SDL_FLIP_NONE:SDL_FLIP_HORIZONTAL;

    SDL_RenderCopyEx(ren,tex,&src,&dst,0.0,NULL,flip);

  }



render_projectile:

  if(e->type==ENEMY_DRONE&&e->droneProjectileActive){

    int px=(int)e->projX+sx, py=(int)e->projY+sy;

    SDL_SetRenderDrawBlendMode(ren,SDL_BLENDMODE_BLEND);

    SDL_Rect halo={px-10,py-10,20,20};

    SDL_SetRenderDrawColor(ren,40,80,255,60);  SDL_RenderFillRect(ren,&halo);

    SDL_Rect mid={px-7,py-7,14,14};

    SDL_SetRenderDrawColor(ren,80,140,255,130); SDL_RenderFillRect(ren,&mid);

    SDL_Rect core={px-4,py-4,8,8};

    SDL_SetRenderDrawColor(ren,160,220,255,255); SDL_RenderFillRect(ren,&core);

    SDL_SetRenderDrawColor(ren,220,240,255,200); SDL_RenderDrawRect(ren,&core);

  }



  if(e->state!=STATE_DEAD&&e->hp<e->maxHp&&e->maxHp>0){

    int bw=dst.w-8; if(bw<4)bw=4;

    int bh=(e->type==ENEMY_CYBORG)?8:5;

    int bx=dst.x+4, by=dst.y-12;

    SDL_SetRenderDrawColor(ren,30,30,30,200);

    SDL_Rect bg={bx,by,bw,bh}; SDL_RenderFillRect(ren,&bg);

    float pct=my_clampf((float)e->hp/(float)e->maxHp,0,1);

    int fw=(int)((float)bw*pct);

    Uint8 rr,gg;

    if(e->type==ENEMY_CYBORG){

      if(pct>0.60f){rr=255;gg=255;}

      else if(pct>0.30f){rr=255;gg=140;}

      else{rr=255;gg=30;}

    } else {rr=(Uint8)(255*(1-pct));gg=(Uint8)(255*pct);}

    SDL_SetRenderDrawColor(ren,rr,gg,0,255);

    SDL_Rect fill={bx,by,fw,bh}; SDL_RenderFillRect(ren,&fill);

    if(e->type==ENEMY_CYBORG){

      SDL_SetRenderDrawColor(ren,255,255,255,180);

      int m1=bx+(int)(bw*0.60f),m2=bx+(int)(bw*0.30f);

      SDL_RenderDrawLine(ren,m1,by,m1,by+bh);

      SDL_RenderDrawLine(ren,m2,by,m2,by+bh);

    }

  }

}



/* ═══════════════════════════════════════════════════════════════════════════

  Level text banner — 5×7 pixel font (unchanged from v6)

  ═══════════════════════════════════════════════════════════════════════════ */



static const Uint8 GLYPH_L[7] ={0x10,0x10,0x10,0x10,0x10,0x10,0x1F};

static const Uint8 GLYPH_E[7] ={0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F};

static const Uint8 GLYPH_V[7] ={0x11,0x11,0x11,0x11,0x11,0x0A,0x04};

static const Uint8 GLYPH_1[7] ={0x04,0x0C,0x04,0x04,0x04,0x04,0x0E};

static const Uint8 GLYPH_2[7] ={0x0E,0x11,0x01,0x02,0x04,0x08,0x1F};

static const Uint8 GLYPH_SP[7]={0x00,0x00,0x00,0x00,0x00,0x00,0x00};



static void renderGlyph(SDL_Renderer *r,const Uint8 *glyph,int cx,int cy,int ps,Uint8 a){

  SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);

  for(int row=0;row<7;row++){

    for(int col=0;col<5;col++){

      if(glyph[row]&(0x10>>col)){

        SDL_SetRenderDrawColor(r,0,0,0,(Uint8)(a*0.45f));

        SDL_Rect sh={cx+col*ps+2,cy+row*ps+2,ps,ps};

        SDL_RenderFillRect(r,&sh);

        SDL_SetRenderDrawColor(r,220,220,255,a);

        SDL_Rect px={cx+col*ps,cy+row*ps,ps,ps};

        SDL_RenderFillRect(r,&px);

      }

    }

  }

}



static void renderLevelText(SDL_Renderer *r,GameLevel lvl,Uint32 startMs,Uint32 now){

  if(now<startMs) return;

  Uint32 el=now-startMs;

  if(el>=LEVEL_TEXT_DURATION_MS) return;

  Uint8 alpha=255;

  if(el>(Uint32)(LEVEL_TEXT_DURATION_MS-LEVEL_TEXT_FADE_MS)){

    Uint32 fe=el-(LEVEL_TEXT_DURATION_MS-LEVEL_TEXT_FADE_MS);

    float t=(float)fe/(float)LEVEL_TEXT_FADE_MS;

    alpha=(Uint8)(255.0f*(1.0f-t));

  }

  const int PS=8;

  const int CW=5*PS+PS;

  const Uint8 *chars[7]={

    GLYPH_L,GLYPH_E,GLYPH_V,GLYPH_E,GLYPH_L,GLYPH_SP,

    (lvl==LEVEL_1)?GLYPH_1:GLYPH_2

  };

  int totalW=7*CW;

  int totalH=7*PS;

  int sx=(WINDOW_W-totalW)/2;

  int sy=(WINDOW_H-totalH)/2;

  SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);

  int pad=PS*2;

  SDL_SetRenderDrawColor(r,0,0,0,(Uint8)(alpha*0.60f));

  SDL_Rect panel={sx-pad,sy-pad,totalW+pad*2,totalH+pad*2};

  SDL_RenderFillRect(r,&panel);

  SDL_SetRenderDrawColor(r,

    (lvl==LEVEL_1)?30:220,

    (lvl==LEVEL_1)?200:50,

    (lvl==LEVEL_1)?80:30,

    alpha);

  SDL_Rect aTop={sx-pad,sy-pad,      totalW+pad*2,4};

  SDL_Rect aBot={sx-pad,sy+totalH+pad-4, totalW+pad*2,4};

  SDL_RenderFillRect(r,&aTop);

  SDL_RenderFillRect(r,&aBot);

  for(int i=0;i<7;i++)

    renderGlyph(r,chars[i],sx+i*CW,sy,PS,alpha);

}



/* ═══════════════════════════════════════════════════════════════════════════

  Player

  ═══════════════════════════════════════════════════════════════════════════ */



typedef struct{

  float x,y,velX,velY,worldX,knockbackVelX;

  bool onGround,facingRight,attacking;

  int  w,h,hp,maxHp;

  Uint32 attackTimer,knockbackEndMs;

} Player;

typedef struct{bool left,right,up,attack;}Input;



static void handleInput(Input *in,bool *run){

  SDL_Event ev;

  while(SDL_PollEvent(&ev)){

    if(ev.type==SDL_QUIT)*run=false;

    if(ev.type==SDL_KEYDOWN&&ev.key.keysym.sym==SDLK_ESCAPE)*run=false;

  }

  const Uint8 *ks=SDL_GetKeyboardState(NULL);

  in->left =ks[SDL_SCANCODE_LEFT] ||ks[SDL_SCANCODE_A];

  in->right =ks[SDL_SCANCODE_RIGHT]||ks[SDL_SCANCODE_D];

  in->up  =ks[SDL_SCANCODE_UP]  ||ks[SDL_SCANCODE_W];

  in->attack=ks[SDL_SCANCODE_SPACE];

}

static void updatePlayer(Player *p,const Input *in,float dt,Uint32 now){

  float vx=0;

  if(in->left){vx=-3.2f;p->facingRight=false;}

  if(in->right){vx=3.2f;p->facingRight=true;}

  p->velX=lerpf(p->velX,vx,0.20f);

  float kb=0;

  if(now<p->knockbackEndMs){

    float t=1-(float)(p->knockbackEndMs-now)/(float)KNOCKBACK_DURATION_MS;

    kb=p->knockbackVelX*(1-t);

  } else p->knockbackVelX=0;

  if(in->up&&p->onGround){p->velY=-10.0f;p->onGround=false;}

  p->velY+=GRAVITY*(dt/16.0f);

  p->x+=(p->velX+kb)*(dt/16.0f);

  p->y+=p->velY*(dt/16.0f);

  float gnd=(float)(GROUND_Y-p->h);

  if(p->y>=gnd){p->y=gnd;p->velY=0;p->onGround=true;}

  if(p->x<0)p->x=0;

  if(p->x>WINDOW_W-p->w)p->x=(float)(WINDOW_W-p->w);

  /* worldX: furthest X reached — never goes backwards */

  if(p->x>p->worldX)p->worldX=p->x;

  if(in->attack&&!p->attacking){p->attacking=true;p->attackTimer=now;}

  if(p->attacking&&(now-p->attackTimer)>300)p->attacking=false;

}

static void renderPlayer(SDL_Renderer *r,const Player *p,Uint32 now,int sx,int sy){

  if(p->attacking){

    Uint8 pulse=(Uint8)(180+75*sinf((float)now*0.05f));

    SDL_SetRenderDrawColor(r,0,pulse,255,255);

  } else SDL_SetRenderDrawColor(r,0,200,255,255);

  SDL_Rect body={(int)p->x+sx,(int)p->y+sy,p->w,p->h};

  SDL_RenderFillRect(r,&body);

  if(p->attacking){

    int ax=p->facingRight?(int)(p->x+p->w)+sx:(int)(p->x-70)+sx;

    SDL_SetRenderDrawColor(r,0,255,200,80);

    SDL_Rect arc={ax,(int)p->y+sy+10,70,p->h-20};

    SDL_RenderFillRect(r,&arc);

  }

}

static void renderGround(SDL_Renderer *r){

  SDL_SetRenderDrawColor(r,14,18,28,255);

  SDL_Rect g={0,GROUND_Y,WINDOW_W,WINDOW_H-GROUND_Y};SDL_RenderFillRect(r,&g);

  SDL_SetRenderDrawColor(r,0,160,100,55);

  SDL_RenderDrawLine(r,0,GROUND_Y,WINDOW_W,GROUND_Y);

  for(int x=0;x<WINDOW_W;x+=80)SDL_RenderDrawLine(r,x,GROUND_Y,x,WINDOW_H);

}

static void renderHUD(SDL_Renderer *r,const Player *p,GameLevel lvl,

           bool gameOver,bool victory){

  float pct=my_clampf((float)p->hp/(float)p->maxHp,0,1);

  SDL_SetRenderDrawColor(r,0,0,0,160);

  SDL_Rect bg={8,8,172,22};SDL_RenderFillRect(r,&bg);

  SDL_SetRenderDrawColor(r,40,40,40,255);

  SDL_Rect hbg={10,10,160,14};SDL_RenderFillRect(r,&hbg);

  SDL_SetRenderDrawColor(r,(Uint8)(255*(1-pct)),(Uint8)(255*pct),0,255);

  SDL_Rect hf={10,10,(int)(160*pct),14};SDL_RenderFillRect(r,&hf);

  SDL_SetRenderDrawColor(r,0,0,0,160);

  SDL_Rect lb={WINDOW_W-120,8,112,22};SDL_RenderFillRect(r,&lb);

  SDL_SetRenderDrawColor(r,

    (lvl==LEVEL_1)?0:220,(lvl==LEVEL_1)?200:40,(lvl==LEVEL_1)?80:40,255);

  SDL_Rect lf={WINDOW_W-118,10,108,14};SDL_RenderFillRect(r,&lf);

  if(victory){

    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(r,0,220,80,100);

    SDL_Rect ov={WINDOW_W/2-150,WINDOW_H/2-30,300,60};SDL_RenderFillRect(r,&ov);

  } else if(gameOver){

    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(r,180,0,0,120);

    SDL_Rect ov={WINDOW_W/2-150,WINDOW_H/2-30,300,60};SDL_RenderFillRect(r,&ov);

  }

}



/* ═══════════════════════════════════════════════════════════════════════════

  Enemy Manager

  [G2] Added lastSpawnWorldX to track scroll progression for spawn gating.

  ═══════════════════════════════════════════════════════════════════════════ */



typedef struct{

  Enemy pool[MAX_ENEMIES];

  int count,dogSlots[CAP_DOG],spiderSlots[CAP_SPIDER],

    droneSlots[CAP_DRONE],cyborgSlot,totalKills;

  GameStage stage;

  GameLevel level;

  SDL_Texture *texDog,*texSpider,*texDrone;

  SDL_Texture *texCI,*texCW,*texCA,*texCH,*texCD;

  float gndDog,gndSpider,gndDrone,gndCyborg;

  Uint32 respawnTimers[MAX_ENEMIES];

  bool  pendingRespawn[MAX_ENEMIES];

  /* [G2] Scroll-spawn tracking */

  float lastSpawnWorldX;

} EnemyManager;



static void EM_init(EnemyManager *em,

  SDL_Texture *tD,SDL_Texture *tS,SDL_Texture *tDr,

  SDL_Texture *tCI,SDL_Texture *tCW,SDL_Texture *tCA,

  SDL_Texture *tCH,SDL_Texture *tCD)

{

  memset(em,0,sizeof(*em));

  em->texDog=tD; em->texSpider=tS; em->texDrone=tDr;

  em->texCI=tCI; em->texCW=tCW; em->texCA=tCA; em->texCH=tCH; em->texCD=tCD;



  /* [G1] use scale-correct ground Y for each type */

  em->gndDog  =dogGroundY();

  em->gndSpider=spiderGroundY();

  em->gndDrone =200.0f;

  em->gndCyborg=cyborgGroundY();



  em->stage=STAGE_DOGS_ONLY;

  em->level=LEVEL_1;

  em->lastSpawnWorldX=0.0f;



  float dogX[CAP_DOG] ={250,600,950};

  float spX[CAP_SPIDER] ={400,800};

  float drX[CAP_DRONE] ={640};

  float drY[CAP_DRONE] ={190};



  int idx=0;

  for(int i=0;i<CAP_DOG;i++){

    em->dogSlots[i]=idx;

    initEnemyBase(&em->pool[idx],ENEMY_DOG,dogX[i],em->gndDog,tD);

    idx++;

  }

  for(int i=0;i<CAP_SPIDER;i++){

    em->spiderSlots[i]=idx;

    initEnemyBase(&em->pool[idx],ENEMY_SPIDER,spX[i],em->gndSpider,tS);

    idx++;

  }

  for(int i=0;i<CAP_DRONE;i++){

    em->droneSlots[i]=idx;

    initEnemyBase(&em->pool[idx],ENEMY_DRONE,drX[i],drY[i],tDr);

    idx++;

  }

  em->cyborgSlot=idx;

  initEnemyBase(&em->pool[idx],ENEMY_CYBORG,640,em->gndCyborg,tCI);

  initCyborgAnimTextures(&em->pool[idx],tCI,tCW,tCA,tCH,tCD);

  idx++;

  em->count=idx;

}



static int EM_countByType(EnemyManager *em,EnemyType t){

  int n=0;

  for(int i=0;i<em->count;i++){

    Enemy *e=&em->pool[i];

    if(e->active&&e->enabled&&e->type==t&&e->state!=STATE_DEAD)n++;

  }

  return n;

}



/* [G4] Count how many enemies are currently attacking */

static int EM_countAttacking(EnemyManager *em){

  int n=0;

  for(int i=0;i<em->count;i++){

    Enemy *e=&em->pool[i];

    if(e->active&&e->enabled&&e->state==STATE_ATTACK)n++;

  }

  return n;

}



static void EM_enableDogs(EnemyManager *em,Uint32 now,float h,float s,float c){

  for(int i=0;i<CAP_DOG;i++){

    Enemy *e=&em->pool[em->dogSlots[i]];

    if(!e->enabled)activateEnemy(e,now,h,s,c);

  }

}

static void EM_enableSpiders(EnemyManager *em,Uint32 now,float h,float s,float c){

  for(int i=0;i<CAP_SPIDER;i++){

    Enemy *e=&em->pool[em->spiderSlots[i]];

    if(!e->enabled)activateEnemy(e,now,h,s,c);

  }

}

static void EM_enableDrones(EnemyManager *em,Uint32 now,float h,float s,float c){

  for(int i=0;i<CAP_DRONE;i++){

    Enemy *e=&em->pool[em->droneSlots[i]];

    if(!e->enabled)activateEnemy(e,now,h,s,c);

  }

}

static void EM_disableMinions(EnemyManager *em){

  for(int i=0;i<em->count;i++)

    if(em->pool[i].type!=ENEMY_CYBORG)deactivateEnemy(&em->pool[i]);

}

static void EM_enableCyborg(EnemyManager *em,Uint32 now){

  Enemy *e=&em->pool[em->cyborgSlot];

  if(!e->enabled){

    e->hp=e->maxHp=e->baseHp; e->speed=SPEED_CYBORG;

    e->attackCooldown=ATTACK_COOLDOWN_MS;

    e->detectRange=DETECT_CYBORG; e->attackRange=ATTACK_CYBORG;

    activateEnemy(e,now,1,1,1);

    e->hp=e->maxHp;

  }

}



static void EM_resetForLevel(EnemyManager *em,Uint32 now,GameLevel lvl){

  em->level=lvl;

  em->totalKills=0;

  em->lastSpawnWorldX=0.0f;

  for(int i=0;i<em->count;i++){

    deactivateEnemy(&em->pool[i]);

    em->pendingRespawn[i]=false;

    em->respawnTimers[i]=0;

  }

  if(lvl==LEVEL_1){

    em->stage=STAGE_DOGS_ONLY;

    EM_enableDogs  (em,now,1.0f,1.0f,1.0f);

    /* Spiders deliberately NOT enabled at L1 start; scroll-spawn brings them */

    SDL_Log("[LEVEL] LEVEL 1 — quota=%d kills",LEVEL1_KILL_QUOTA);

  } else {

    em->stage=STAGE_BOSS;

    EM_enableDogs  (em,now,LVL2_HP_MULT,LVL2_SPD_MULT,LVL2_CD_MULT);

    EM_enableSpiders(em,now,LVL2_HP_MULT,LVL2_SPD_MULT,LVL2_CD_MULT);

    EM_enableDrones (em,now,LVL2_HP_MULT,LVL2_SPD_MULT,LVL2_CD_MULT);

    EM_enableCyborg (em,now);

    SDL_Log("[LEVEL] LEVEL 2 — All enemies + Boss");

  }

}



/*

 * EM_update — main enemy manager tick.

 * [G2] playerWorldX passed in; respawns are gated on player progression.

 * [G4] attackerCount computed once per frame; canAttack = (count < cap).

 */

static void EM_update(EnemyManager *em,float px,float py,float playerWorldX,

           Uint32 now,float dt,ScreenShake *shake)

{

  /* [G4] Compute attacker count BEFORE updating AI so cap is per-frame accurate */

  int attackerCount=EM_countAttacking(em);



  /* Tick all active enemies */

  for(int i=0;i<em->count;i++){

    Enemy *e=&em->pool[i];

    if(!e->active||!e->enabled) continue;

    /* [G4] This enemy may attack only if below the global cap,

        OR if it is already attacking (don't kick it out mid-attack). */

    bool alreadyAttacking=(e->state==STATE_ATTACK);

    bool canAttack=alreadyAttacking||(attackerCount<MAX_SIMULTANEOUS_ATTACKERS);



    bool wasDead=(e->state==STATE_DEAD&&e->animDone);

    updateEnemy(e,px,py,now,dt,shake,canAttack);

    /* Update count if this enemy just entered ATTACK this frame */

    if(!alreadyAttacking&&e->state==STATE_ATTACK) attackerCount++;



    if(!wasDead&&e->state==STATE_DEAD&&e->animDone&&e->type!=ENEMY_CYBORG){

      em->totalKills++;

      if(!em->pendingRespawn[i]){

        em->pendingRespawn[i]=true;

        int delay=(em->level==LEVEL_2)?LEVEL2_RESPAWN_DELAY_MS:

             (em->stage>=STAGE_DOGS_SPIDERS_DRONES)?RESPAWN_DELAY_MIN:

             LEVEL1_RESPAWN_DELAY_MS;

        em->respawnTimers[i]=now+(Uint32)delay;

      }

    }

  }



  /* ── [G2] Scroll-based respawn pass ─────────────────────────────────

   * Enemies respawn ahead of the player's furthest point, not on top.

   * Player must have advanced SPAWN_STEP px since last respawn wave.

   * ──────────────────────────────────────────────────────────────────*/

  bool playerAdvanced=(playerWorldX-em->lastSpawnWorldX)>=SPAWN_STEP;



  for(int i=0;i<em->count;i++){

    Enemy *e=&em->pool[i];

    if(!e->active||e->type==ENEMY_CYBORG||!em->pendingRespawn[i]) continue;

    if(now<em->respawnTimers[i]) continue;



    /* Level 1 stage gate */

    if(em->level==LEVEL_1){

      bool ok=false;

      switch(em->stage){

        case STAGE_DOGS_ONLY:  ok=(e->type==ENEMY_DOG); break;

        case STAGE_DOGS_SPIDERS: ok=(e->type==ENEMY_DOG||e->type==ENEMY_SPIDER); break;

        default: ok=false; break;

      }

      if(!ok){em->pendingRespawn[i]=false;continue;}

    }



    /* Cap check */

    int cap=0;

    if(e->type==ENEMY_DOG)  cap=CAP_DOG;

    if(e->type==ENEMY_SPIDER) cap=CAP_SPIDER;

    if(e->type==ENEMY_DRONE) cap=CAP_DRONE;

    if(cap<=0){em->pendingRespawn[i]=false;continue;}

    if(EM_countByType(em,e->type)>=cap){em->pendingRespawn[i]=false;continue;}



    /* [G2] Gate on player progression */

    if(!playerAdvanced&&em->level==LEVEL_1) continue;



    /* [G2] Compute spawn X ahead of player, clamped to screen */

    float randOffset=(float)(rand()%(int)SPAWN_TRIGGER_RANGE);

    float spawnX=playerWorldX+SPAWN_TRIGGER_AHEAD+randOffset;

    spawnX=my_clampf(spawnX,e->frameW,(float)(WINDOW_W-e->frameW*2));

    e->spawnX=spawnX;

    e->patrolA=spawnX-PATROL_HALF;

    e->patrolB=spawnX+PATROL_HALF;



    float h=1.0f,s=1.0f,c=1.0f;

    if(em->level==LEVEL_2){h=LVL2_HP_MULT;s=LVL2_SPD_MULT;c=LVL2_CD_MULT;}

    else if(em->stage==STAGE_DOGS_SPIDERS){h=1.3f;s=1.15f;c=0.85f;}

    activateEnemy(e,now,h,s,c);

    em->pendingRespawn[i]=false;

    /* Update spawn watermark so next wave waits for more progress */

    em->lastSpawnWorldX=playerWorldX;

    SDL_Log("[SPAWN] %s at x=%.0f (worldX=%.0f)",

      e->type==ENEMY_DOG?"Dog":"Spider",spawnX,playerWorldX);

  }

}



/* [G2] Stage machine — also enables spiders once player reaches KILLS_FOR_STAGE2 */

static void EM_manageStage(EnemyManager *em,Uint32 now){

  switch(em->stage){

    case STAGE_DOGS_ONLY:

      if(EM_countByType(em,ENEMY_DOG)==0){

        bool any=false;

        for(int i=0;i<CAP_DOG;i++)

          if(em->pendingRespawn[em->dogSlots[i]]){any=true;break;}

        if(!any)EM_enableDogs(em,now,1,1,1);

      }

      if(em->totalKills>=KILLS_FOR_STAGE2){

        em->stage=STAGE_DOGS_SPIDERS;

        SDL_Log("[STAGE L1] Dogs+Spiders (kills=%d)",em->totalKills);

        EM_enableSpiders(em,now,1.3f,1.15f,0.85f);

      }

      break;

    case STAGE_DOGS_SPIDERS:

      /* Level 1 ends here via LEVEL1_KILL_QUOTA — no further escalation */

      break;

    default:break;

  }

}



/* ═══════════════════════════════════════════════════════════════════════════

  main

  ═══════════════════════════════════════════════════════════════════════════ */



int main(int argc,char *argv[]){

  (void)argc;(void)argv;

  srand((unsigned)SDL_GetTicks());



  if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)!=0){

    SDL_Log("SDL_Init:%s",SDL_GetError());return 1;}

  if(!(IMG_Init(IMG_INIT_PNG)&IMG_INIT_PNG)){

    SDL_Log("IMG_Init:%s",IMG_GetError());SDL_Quit();return 1;}



  SDL_Window *win=SDL_CreateWindow("Cyberpunk Enemy System",

    SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,WINDOW_W,WINDOW_H,SDL_WINDOW_SHOWN);

  if(!win){SDL_Log("Window:%s",SDL_GetError());IMG_Quit();SDL_Quit();return 1;}



  SDL_Renderer *ren=SDL_CreateRenderer(win,-1,

    SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);

  if(!ren){

    SDL_Log("Renderer:%s",SDL_GetError());

    SDL_DestroyWindow(win);IMG_Quit();SDL_Quit();return 1;}

  SDL_SetRenderDrawBlendMode(ren,SDL_BLENDMODE_BLEND);



  SDL_Texture *tDog =loadTexture(ren,"Dog.png");

  SDL_Texture *tSpider=loadTexture(ren,"Spider.png");

  SDL_Texture *tDrone =loadTexture(ren,"Drone.png");

  SDL_Texture *tCybI =loadTexture(ren,"Cyborg_idle.png");

  SDL_Texture *tCybW =loadTexture(ren,"Cyborg_run.png");

  SDL_Texture *tCybA =loadTexture(ren,"Cyborg_attack1.png");

  SDL_Texture *tCybH =loadTexture(ren,"Cyborg_hurt.png");

  SDL_Texture *tCybD =loadTexture(ren,"Cyborg_death.png");



  EnemyManager em;

  EM_init(&em,tDog,tSpider,tDrone,tCybI,tCybW,tCybA,tCybH,tCybD);



  Uint32 startNow=SDL_GetTicks();

  EM_resetForLevel(&em,startNow,LEVEL_1);



  bool  showLevelText  =true;

  Uint32 levelTextStartMs =startNow;



  Player player={

    .x=WINDOW_W*0.15f,.y=(float)(GROUND_Y-64),

    .worldX=WINDOW_W*0.15f,.w=30,.h=60,

    .hp=100,.maxHp=100,.onGround=true,.facingRight=true

  };



  bool victory   =false;

  bool gameOver   =false;

  bool level2Entered=false;



  ScreenShake shake={0,0,0,false};

  Uint32 lastHit[MAX_ENEMIES];

  memset(lastHit,0,sizeof(lastHit));



  bool  running=true;

  Input input={0};

  Uint32 prev=SDL_GetTicks();



  while(running){

    Uint32 now=SDL_GetTicks();

    float dt=(float)(now-prev);

    if(dt>50)dt=50; if(dt<1)dt=1;

    prev=now;



    handleInput(&input,&running);



    if(showLevelText&&(now-levelTextStartMs)>=LEVEL_TEXT_DURATION_MS)

      showLevelText=false;



    if(!victory&&!gameOver){

      updatePlayer(&player,&input,dt,now);



      /* Level 1 → Level 2 transition */

      if(em.level==LEVEL_1&&!level2Entered&&

        em.totalKills>=LEVEL1_KILL_QUOTA)

      {

        level2Entered=true;

        EM_resetForLevel(&em,now,LEVEL_2);

        player.x=WINDOW_W*0.15f;

        player.velX=player.velY=0.0f;

        showLevelText  =true;

        levelTextStartMs=now;

      }



      if(em.level==LEVEL_1) EM_manageStage(&em,now);



      if(em.level==LEVEL_2){

        Enemy *boss=&em.pool[em.cyborgSlot];

        if(boss->enabled&&boss->state==STATE_DEAD&&boss->animDone){

          victory=true;

          SDL_Log("PLAYER WON - GAME COMPLETED");

        }

      }



      if(player.hp<=0){

        gameOver=true;

        SDL_Log("[GAME] PLAYER DEFEATED — GAME OVER");

      }



      float pCx=player.x+player.w*0.5f;

      float pCy=player.y+player.h*0.5f;



      /* [G2] Pass playerWorldX into EM_update for scroll-based spawning */

      EM_update(&em,pCx,pCy,player.worldX,now,dt,&shake);



      /* Enemy body → player */

      for(int i=0;i<em.count;i++){

        Enemy *e=&em.pool[i];

        if(!e->active||!e->enabled) continue;

        if(enemyAttackHitPlayer(e,player.x,player.y,player.w,player.h)){

          e->attackHit=true;

          player.hp-=(e->type==ENEMY_CYBORG)?18:8;

          if(player.hp<0)player.hp=0;

          float dir=(player.x+player.w*0.5f>e->x+e->frameW*0.5f)?1.0f:-1.0f;

          player.knockbackVelX=dir*KNOCKBACK_SPEED_PLAYER;

          player.knockbackEndMs=now+KNOCKBACK_DURATION_MS;

          triggerShake(&shake,SHAKE_MAGNITUDE_HIT,SHAKE_DURATION_MS,now);

        }

      }



      /* Drone projectile → player */

      for(int i=0;i<em.count;i++){

        Enemy *e=&em.pool[i];

        if(!e->active||!e->enabled) continue;

        if(e->type!=ENEMY_DRONE||!e->droneProjectileActive) continue;

        SDL_Rect pb={(int)(e->projX-4),(int)(e->projY-4),8,8};

        SDL_Rect plb={(int)player.x,(int)player.y,player.w,player.h};

        SDL_Rect hit;

        if(SDL_IntersectRect(&pb,&plb,&hit)){

          e->droneProjectileActive=false;

          player.hp-=8;

          if(player.hp<0)player.hp=0;

          float dir=(player.x+player.w*0.5f>e->x+e->frameW*0.5f)?1.0f:-1.0f;

          player.knockbackVelX=dir*KNOCKBACK_SPEED_PLAYER;

          player.knockbackEndMs=now+KNOCKBACK_DURATION_MS;

          triggerShake(&shake,SHAKE_MAGNITUDE_HIT,SHAKE_DURATION_MS,now);

        }

      }



      /* Player attack → enemies */

      for(int i=0;i<em.count;i++){

        Enemy *e=&em.pool[i];

        if(!e->active||!e->enabled||!player.attacking) continue;

        float rx=player.facingRight?(player.x+player.w):(player.x-70);

        float eCx=e->x+e->frameW*0.5f,eCy=e->y+e->frameH*0.5f;

        float dx=my_absf(eCx-(rx+35)),dy=my_absf(eCy-(player.y+player.h*0.5f));

        if(dx<65&&dy<55&&(now-lastHit[i])>350){

          hurtEnemy(e,20,player.x+player.w*0.5f,&shake);

          lastHit[i]=now;

        }

      }

    } /* end !victory && !gameOver */



    /* Screen shake */

    int shakeX=0,shakeY=0;

    if(shake.active){

      Uint32 el=now-shake.startMs;

      if(el>=shake.durationMs)shake.active=false;

      else{

        float t=1-(float)el/(float)shake.durationMs;

        float ang=(float)el*0.08f;

        shakeX=(int)(cosf(ang)*shake.magnitude*t);

        shakeY=(int)(sinf(ang*1.3f)*shake.magnitude*t);

      }

    }



    /* ── Render ───────────────────────────────────────────────────── */

    SDL_SetRenderDrawColor(ren,6,8,16,255);

    SDL_RenderClear(ren);

    SDL_SetRenderDrawColor(ren,0,0,0,28);

    for(int y=0;y<WINDOW_H;y+=3)SDL_RenderDrawLine(ren,0,y,WINDOW_W,y);

    if(em.level==LEVEL_2&&!victory){

      Enemy *boss=&em.pool[em.cyborgSlot];

      if(boss->enabled&&boss->state!=STATE_DEAD){

        SDL_SetRenderDrawColor(ren,120,0,0,20);

        SDL_Rect full={0,0,WINDOW_W,WINDOW_H};SDL_RenderFillRect(ren,&full);

      }

    }

    renderGround(ren);

    for(int i=0;i<em.count;i++) renderEnemy(&em.pool[i],ren,shakeX,shakeY);

    renderPlayer(ren,&player,now,shakeX,shakeY);

    renderHUD(ren,&player,em.level,gameOver,victory);

    if(showLevelText)

      renderLevelText(ren,em.level,levelTextStartMs,now);

    SDL_RenderPresent(ren);



    Uint32 el=SDL_GetTicks()-now;

    if(el<(Uint32)FRAME_MS)SDL_Delay(FRAME_MS-el);

  }



  /* Cleanup */

  SDL_DestroyTexture(tDog);  SDL_DestroyTexture(tSpider);

  SDL_DestroyTexture(tDrone); SDL_DestroyTexture(tCybI);

  SDL_DestroyTexture(tCybW); SDL_DestroyTexture(tCybA);

  SDL_DestroyTexture(tCybH); SDL_DestroyTexture(tCybD);

  SDL_DestroyRenderer(ren);

  SDL_DestroyWindow(win);

  IMG_Quit();

  SDL_Quit();

  return 0;

}
