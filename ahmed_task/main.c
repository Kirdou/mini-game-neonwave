#include <stdio.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

/* ─────────────────────────────────────────────
  CONSTANTS
  ───────────────────────────────────────────── */

#define WORLD_WIDTH  2000  /* Total width of the game world */
#define WORLD_HEIGHT 1500  /* Total height of the game world */

#define MINIMAP_X   20  /* Mini-map top-left X on screen */
#define MINIMAP_Y   20  /* Mini-map top-left Y on screen */
#define MINIMAP_SCALE 0.1f /* 1 world unit = 0.1 mini-map px */

#define MINIMAP_MAX_ENTITIES 64 /* Hard cap on tracked entities  */

/* Path to the BMP image used as the mini-map background */
#define MINIMAP_BG_PATH "minimap_bg.bmp"

/* WINDOW / VIEWPORT SETTINGS */
#define SCREEN_WIDTH  800
#define SCREEN_HEIGHT 600
#define PLAYER_SPEED  3

/* Path to your map image */
#define MAP_IMAGE_PATH "map.png"

/* COLORS */
static const SDL_Color COLOR_PLAYER   = { 50, 180, 255, 255 }; /* Sky blue */

/* ─────────────────────────────────────────────
  STRUCT: Personne
  ───────────────────────────────────────────── */
typedef struct {
  float x;
  float y;
  int  w;
  int  h;
} Personne;

/* ─────────────────────────────────────────────
  STRUCT: EntityColor
  ───────────────────────────────────────────── */
typedef struct {
  SDL_Rect rect;
  SDL_Color color;
} EntityColor;

/* ─────────────────────────────────────────────
  STRUCT: MiniMap
  ───────────────────────────────────────────── */
typedef struct {
  int    x;
  int    y;
  int    width;
  int    height;
  float  scale;
  SDL_Rect playerRect;
  SDL_Texture *bgTexture;
  EntityColor entities[MINIMAP_MAX_ENTITIES];
  int     entityCount;
} MiniMap;

/* ─────────────────────────────────────────────
  FUNCTION DECLARATIONS
  ───────────────────────────────────────────── */
static void initMiniMap(MiniMap *m, SDL_Renderer *renderer);
static void destroyMiniMap(MiniMap *m);
static void afficherMiniMap(MiniMap m, SDL_Renderer *renderer);
static void miseAJourMiniMap(MiniMap *m, Personne *p);
static void ajouterEntites(MiniMap *m, Personne *entities, int count, SDL_Color color);
static void afficherEntites(MiniMap m, SDL_Renderer *renderer);
static SDL_Texture *loadMapTexture(SDL_Renderer *renderer, const char *path);
static void renderMap(SDL_Renderer *renderer, SDL_Texture *mapTex, SDL_Rect camera);
static void updateCamera(SDL_Rect *cam, const Personne *p);
static void drawPersonne(SDL_Renderer *renderer, const Personne *p, SDL_Color color, SDL_Rect camera);
static SDL_Rect worldToMiniMap(const MiniMap *m, const Personne *p);

/* ─────────────────────────────────────────────
  Internal helper: worldToMiniMap
  ───────────────────────────────────────────── */
static SDL_Rect worldToMiniMap(const MiniMap *m, const Personne *p)
{
  SDL_Rect r;
  r.x = m->x + (int)(p->x * m->scale);
  r.y = m->y + (int)(p->y * m->scale);
  r.w = (int)(p->w * m->scale); if (r.w < 3) r.w = 3;
  r.h = (int)(p->h * m->scale); if (r.h < 3) r.h = 3;

  if (r.x < m->x)          r.x = m->x;
  if (r.y < m->y)          r.y = m->y;
  if (r.x + r.w > m->x + m->width) r.x = m->x + m->width - r.w;
  if (r.y + r.h > m->y + m->height) r.y = m->y + m->height - r.h;

  return r;
}

/* ─────────────────────────────────────────────
  initMiniMap
  ───────────────────────────────────────────── */
static void initMiniMap(MiniMap *m, SDL_Renderer *renderer)
{
  m->x   = MINIMAP_X;
  m->y   = MINIMAP_Y;
  m->scale = MINIMAP_SCALE;
  m->width = (int)(WORLD_WIDTH * MINIMAP_SCALE);
  m->height = (int)(WORLD_HEIGHT * MINIMAP_SCALE);

  m->playerRect.x = m->x;
  m->playerRect.y = m->y;
  m->playerRect.w = 4;
  m->playerRect.h = 4;

  m->entityCount = 0;
  m->bgTexture  = NULL;

  SDL_Surface *surface = SDL_LoadBMP(MINIMAP_BG_PATH);
  if (!surface) {
    fprintf(stderr,
      "[MiniMap] Warning: could not load '%s': %s\n"
      "     Using solid colour background instead.\n",
      MINIMAP_BG_PATH, SDL_GetError());
    return;
  }

  m->bgTexture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_FreeSurface(surface);

  if (!m->bgTexture)
    fprintf(stderr, "[MiniMap] Warning: could not create texture: %s\n", SDL_GetError());
  else
    printf("[MiniMap] Background loaded from '%s' (%dx%d)\n",
        MINIMAP_BG_PATH, m->width, m->height);
}

/* ─────────────────────────────────────────────
  destroyMiniMap
  ───────────────────────────────────────────── */
static void destroyMiniMap(MiniMap *m)
{
  if (m->bgTexture) {
    SDL_DestroyTexture(m->bgTexture);
    m->bgTexture = NULL;
  }
}

/* ─────────────────────────────────────────────
  afficherMiniMap
  ───────────────────────────────────────────── */
static void afficherMiniMap(MiniMap m, SDL_Renderer *renderer)
{
  SDL_Rect bg = { m.x, m.y, m.width, m.height };

  if (m.bgTexture) {
    SDL_RenderCopy(renderer, m.bgTexture, NULL, &bg);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 90);
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
  } else {
    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    SDL_RenderFillRect(renderer, &bg);
  }

  SDL_SetRenderDrawColor(renderer, 160, 160, 160, 255);
  SDL_RenderDrawRect(renderer, &bg);

  SDL_SetRenderDrawColor(renderer, 255, 220, 50, 255);
  SDL_RenderFillRect(renderer, &m.playerRect);
}

/* ─────────────────────────────────────────────
  miseAJourMiniMap
  ───────────────────────────────────────────── */
static void miseAJourMiniMap(MiniMap *m, Personne *p)
{
  m->playerRect = worldToMiniMap(m, p);
}

/* ─────────────────────────────────────────────
  ajouterEntites
  ───────────────────────────────────────────── */
static void ajouterEntites(MiniMap *m, Personne *entities, int count, SDL_Color color)
{
  int i;
  for (i = 0; i < count; i++) {
    if (m->entityCount >= MINIMAP_MAX_ENTITIES) break;
    m->entities[m->entityCount].rect = worldToMiniMap(m, &entities[i]);
    m->entities[m->entityCount].color = color;
    m->entityCount++;
  }
}

/* ─────────────────────────────────────────────
  afficherEntites
  ───────────────────────────────────────────── */
static void afficherEntites(MiniMap m, SDL_Renderer *renderer)
{
  int i;
  for (i = 0; i < m.entityCount; i++) {
    SDL_Color c = m.entities[i].color;
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(renderer, &m.entities[i].rect);
  }
}

/* ─────────────────────────────────────────────
  loadMapTexture
  Loads the world map image as an SDL_Texture.
  ───────────────────────────────────────────── */
static SDL_Texture *loadMapTexture(SDL_Renderer *renderer, const char *path)
{
  SDL_Surface *surface = IMG_Load(path);
  if (!surface) {
    fprintf(stderr,
      "[Map] IMG_Load failed for '%s': %s\n"
      "   Falling back to SDL_LoadBMP...\n",
      path, IMG_GetError());
    surface = SDL_LoadBMP(path);
    if (!surface) {
      fprintf(stderr, "[Map] SDL_LoadBMP also failed: %s\n", SDL_GetError());
      return NULL;
    }
  }

  SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_FreeSurface(surface);

  if (!tex) {
    fprintf(stderr, "[Map] Could not create GPU texture: %s\n", SDL_GetError());
    return NULL;
  }

  int tw, th;
  SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
  printf("[Map] Loaded '%s' → GPU texture %dx%d\n", path, tw, th);

  if (tw != WORLD_WIDTH || th != WORLD_HEIGHT) {
    fprintf(stderr,
      "[Map] WARNING: image is %dx%d but world is %dx%d — will stretch.\n",
      tw, th, WORLD_WIDTH, WORLD_HEIGHT);
  }

  return tex;
}

/* ─────────────────────────────────────────────
  renderMap
  Renders only the camera-visible slice of the
  world map texture onto the full screen.
  ───────────────────────────────────────────── */
static void renderMap(SDL_Renderer *renderer, SDL_Texture *mapTex, SDL_Rect camera)
{
  if (!mapTex) {
    SDL_SetRenderDrawColor(renderer, 25, 25, 35, 255);
    SDL_RenderClear(renderer);
    return;
  }

  SDL_Rect srcRect = camera;
  SDL_Rect dstRect = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
  SDL_RenderCopy(renderer, mapTex, &srcRect, &dstRect);
}

/* ─────────────────────────────────────────────
  updateCamera
  Centers on player, clamps to world bounds.
  ───────────────────────────────────────────── */
static void updateCamera(SDL_Rect *cam, const Personne *p)
{
  cam->x = (int)(p->x + p->w / 2) - SCREEN_WIDTH / 2;
  cam->y = (int)(p->y + p->h / 2) - SCREEN_HEIGHT / 2;

  if (cam->x < 0)              cam->x = 0;
  if (cam->y < 0)              cam->y = 0;
  if (cam->x > WORLD_WIDTH - SCREEN_WIDTH) cam->x = WORLD_WIDTH - SCREEN_WIDTH;
  if (cam->y > WORLD_HEIGHT - SCREEN_HEIGHT) cam->y = WORLD_HEIGHT - SCREEN_HEIGHT;
}

/* ─────────────────────────────────────────────
  drawPersonne
  Renders any entity at its world position
  offset by the camera (screen-space).
  ───────────────────────────────────────────── */
static void drawPersonne(SDL_Renderer *renderer, const Personne *p, SDL_Color color, SDL_Rect camera)
{
  SDL_Rect r = {
    (int)p->x - camera.x,
    (int)p->y - camera.y,
    p->w,
    p->h
  };
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderFillRect(renderer, &r);
  SDL_SetRenderDrawColor(renderer,
    (Uint8)(color.r / 2), (Uint8)(color.g / 2), (Uint8)(color.b / 2), 255);
  SDL_RenderDrawRect(renderer, &r);
}

/* ─────────────────────────────────────────────
  main
  ───────────────────────────────────────────── */
int main(void)
{
  /* ── 1. Init SDL + SDL_image ── */
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
    return 1;
  }

  if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
    fprintf(stderr, "[SDL_image] PNG init failed: %s\n", IMG_GetError());
  }

  SDL_Window *window = SDL_CreateWindow(
    "Cyberpunk City — Scrolling Map",
    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    SCREEN_WIDTH, SCREEN_HEIGHT,
    SDL_WINDOW_SHOWN
  );
  if (!window) {
    fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
    IMG_Quit(); SDL_Quit();
    return 1;
  }

  SDL_Renderer *renderer = SDL_CreateRenderer(
    window, -1,
    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
  );
  if (!renderer) {
    fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    IMG_Quit(); SDL_Quit();
    return 1;
  }

  /* ── 2. Load world map as GPU texture ── */
  SDL_Texture *mapTexture = loadMapTexture(renderer, MAP_IMAGE_PATH);

  /* ── 3. Camera (world viewport) ── */
  SDL_Rect camera = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };

  /* ── 4. Player ── */
  Personne player = { 400.0f, 300.0f, 32, 32 };

  /* ── 7. Mini-map ── */
  MiniMap minimap;
  initMiniMap(&minimap, renderer);

  /* ── 8. Main loop ── */
  int running = 1;
  SDL_Event event;

  while (running) {
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) running = 0;
      if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = 0;
    }

    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) player.x -= PLAYER_SPEED;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) player.x += PLAYER_SPEED;
    if (keys[SDL_SCANCODE_UP]  || keys[SDL_SCANCODE_W]) player.y -= PLAYER_SPEED;
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) player.y += PLAYER_SPEED;

    if (player.x < 0)            player.x = 0;
    if (player.y < 0)            player.y = 0;
    if (player.x + player.w > WORLD_WIDTH) player.x = WORLD_WIDTH - player.w;
    if (player.y + player.h > WORLD_HEIGHT) player.y = WORLD_HEIGHT - player.h;

    updateCamera(&camera, &player);

    miseAJourMiniMap(&minimap, &player);
    minimap.entityCount = 0;

    /* RENDER */
    renderMap(renderer, mapTexture, camera);

    drawPersonne(renderer, &player, COLOR_PLAYER, camera);

    afficherMiniMap(minimap, renderer);
    afficherEntites(minimap, renderer);

    SDL_RenderPresent(renderer);
  }

  if (mapTexture) SDL_DestroyTexture(mapTexture);
  destroyMiniMap(&minimap);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  IMG_Quit();
  SDL_Quit();

  return 0;
}
