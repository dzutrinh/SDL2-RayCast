/*
 * ray.c — SDL2 raycasting demo
 * Ported from TPRAY.PAS
 *
 * Controls : W / S   — move forward / back
 *            A / D   — rotate left / right
 *            ESC     — quit
 *
 * Build    : make
 * or       : gcc ray.c -o ray -lm -lSDL2
 */

#include <SDL2/SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/*  Configuration                                                              */
/* -------------------------------------------------------------------------- */

#define MAP_W       24
#define MAP_H       24

#define SCREEN_W    320
#define SCREEN_H    200
#define SCALE       3           /* logical → physical pixel scale factor      */
#define TITLE       "Raycasting Demo  [W/S=move  A/D=rotate  ESC=quit]"

/* movement / rotation speeds (units or radians per second) */
#define MV_SPEED    3.0
#define RT_SPEED    2.0

/* -------------------------------------------------------------------------- */
/*  Colour types & helpers                                                     */
/* -------------------------------------------------------------------------- */

typedef struct { uint8_t r, g, b; } RGB;

/* Pack an RGB into a 32-bit ARGB8888 pixel (alpha = 0xFF). */
static inline uint32_t rgb_pack(RGB c)
{
    return 0xFF000000u | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
}

/* Scale all channels by factor f  (0.0 – 1.0). */
static inline RGB rgb_dim(RGB c, double f)
{
    return (RGB){
        (uint8_t)(c.r * f),
        (uint8_t)(c.g * f),
        (uint8_t)(c.b * f)
    };
}

/* -------------------------------------------------------------------------- */
/*  Colour table                                                               */
/*                                                                             */
/*  Base values are the 8-bit equivalents of the VGA default-palette          */
/*  entries used by the Pascal original (6-bit DAC × 4):                      */
/*    type 1 → $28 = idx 40  → (168,  0,  0)  red                            */
/*    type 2 → $2B = idx 43  → (  0,168,  0)  green                          */
/*    type 3 → $2F = idx 47  → (168,168,  0)  yellow                         */
/*    type 4 → $37 = idx 55  → (  0,  0,168)  blue                           */
/*    type 5 → $3C = idx 60  → (168,  0,168)  magenta                        */
/*    ceiling → $7E = idx 126 → (168,252,252)  light cyan                    */
/*    floor   → 19           → ( 44, 44, 44)  dark grey                      */
/*                                                                             */
/*  Each +72 palette step in the original ≈ ×0.60 brightness here.           */
/* -------------------------------------------------------------------------- */

#define SHADE  0.60     /* brightness factor per shade level */

static const RGB WALL_BASE[6] = {
    {  0,   0,   0},   /* 0 — unused                                         */
    {168,   0,   0},   /* 1 — red     ($28)                                  */
    {  0, 168,   0},   /* 2 — green   ($2B)                                  */
    {168, 168,   0},   /* 3 — yellow  ($2F)                                  */
    {  0,   0, 168},   /* 4 — blue    ($37)                                  */
    {168,   0, 168},   /* 5 — magenta ($3C)                                  */
};

static const RGB CEILING_COL = {168, 252, 252};  /* $7E — light cyan         */
static const RGB FLOOR_COL   = { 44,  44,  44};  /* 19  — dark grey          */

/*
 * Return the wall colour for a given type and shading state.
 * shade_count : 0 = full brightness
 *               1 = Y-face OR far (perpd > 4)
 *               2 = Y-face AND far
 */
static RGB wall_color(int type, int side, double perpd)
{
    if (type < 1 || type > 5) return (RGB){0, 0, 0};
    RGB c = WALL_BASE[type];
    if (side == 1)   c = rgb_dim(c, SHADE);
    if (perpd > 4.0) c = rgb_dim(c, SHADE);
    return c;
}

/* -------------------------------------------------------------------------- */
/*  Map                                                                        */
/* -------------------------------------------------------------------------- */

typedef uint8_t Map[MAP_W][MAP_H];

static bool load_map(const char *path, Map map)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "load_map: cannot open '%s'\n", path);
        return false;
    }
    for (int i = 0; i < MAP_W; i++) {
        for (int j = 0; j < MAP_H; j++) {
            unsigned v;
            if (fscanf(f, "%u", &v) != 1) {
                fprintf(stderr, "load_map: parse error at [%d][%d]\n", i, j);
                fclose(f);
                return false;
            }
            map[i][j] = (uint8_t)v;
        }
    }
    fclose(f);
    return true;
}

/* -------------------------------------------------------------------------- */
/*  Pixel-buffer helpers                                                       */
/* -------------------------------------------------------------------------- */

static void fill_hband(uint32_t *buf, int y1, int y2, uint32_t col)
{
    for (int y = y1; y < y2; y++) {
        uint32_t *row = buf + (y * SCREEN_W);
        for (int x = 0; x < SCREEN_W; x++) row[x] = col;
    }
}

static void draw_vline(uint32_t *buf, int x, int y1, int y2, RGB c)
{
    if (y1 < 0)          y1 = 0;
    if (y2 >= SCREEN_H)  y2 = SCREEN_H - 1;
    uint32_t col = rgb_pack(c);
    for (int y = y1; y <= y2; y++)
        buf[y * SCREEN_W + x] = col;
}

/* -------------------------------------------------------------------------- */
/*  Main                                                                       */
/* -------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    /* --- load map --- */
    Map map;
    if (!load_map("world.txt", map)) return 1;

    /* --- SDL2 init --- */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow(
        TITLE,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W * SCALE, SCREEN_H * SCALE,
        SDL_WINDOW_SHOWN
    );
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *rend = SDL_CreateRenderer(
        win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!rend) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    /* Scale the 320×200 logical surface up to fill the window. */
    SDL_RenderSetLogicalSize(rend, SCREEN_W, SCREEN_H);

    SDL_Texture *tex = SDL_CreateTexture(
        rend,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_W, SCREEN_H
    );
    if (!tex) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        SDL_DestroyRenderer(rend);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    uint32_t *pixels = (uint32_t *)malloc(SCREEN_W * SCREEN_H * sizeof(uint32_t));
    if (!pixels) {
        fprintf(stderr, "out of memory\n");
        SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(rend);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    /* --- player state  (same initial values as the Pascal original) --- */
    double xpos =  22.0,  ypos =  12.0;   /* position                       */
    double drx  =  -1.0,  dry  =   0.0;   /* direction vector               */
    double px   =   0.0,  py   =   0.66;  /* camera plane (FOV ≈ 66°)       */

    const uint8_t *ks = SDL_GetKeyboardState(NULL);

    uint64_t t_prev = SDL_GetPerformanceCounter();
    const uint64_t t_freq = SDL_GetPerformanceFrequency();

    bool done = false;

    while (!done) {

        /* --- delta time --- */
        uint64_t t_now = SDL_GetPerformanceCounter();
        double dt = (double)(t_now - t_prev) / (double)t_freq;
        t_prev = t_now;
        /* cap dt to avoid huge jumps after pauses / debugger breaks */
        if (dt > 0.1) dt = 0.1;

        /* --- events --- */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) done = true;
            if (ev.type == SDL_KEYDOWN &&
                ev.key.keysym.sym == SDLK_ESCAPE) done = true;
        }

        /* --- continuous movement (keyboard state) --- */
        double mv = MV_SPEED * dt;
        double rt = RT_SPEED * dt;

        if (ks[SDL_SCANCODE_W]) {
            if (!map[(int)(xpos + drx * mv)][(int) ypos           ]) xpos += drx * mv;
            if (!map[(int) xpos            ][(int)(ypos + dry * mv)]) ypos += dry * mv;
        }
        if (ks[SDL_SCANCODE_S]) {
            if (!map[(int)(xpos - drx * mv)][(int) ypos           ]) xpos -= drx * mv;
            if (!map[(int) xpos            ][(int)(ypos - dry * mv)]) ypos -= dry * mv;
        }
        if (ks[SDL_SCANCODE_D]) {   /* rotate right */
            double ox = drx, op = px;
            drx = ox * cos(-rt) - dry * sin(-rt);
            dry = ox * sin(-rt) + dry * cos(-rt);
            px  = op * cos(-rt) - py  * sin(-rt);
            py  = op * sin(-rt) + py  * cos(-rt);
        }
        if (ks[SDL_SCANCODE_A]) {   /* rotate left */
            double ox = drx, op = px;
            drx = ox * cos(rt) - dry * sin(rt);
            dry = ox * sin(rt) + dry * cos(rt);
            px  = op * cos(rt) - py  * sin(rt);
            py  = op * sin(rt) + py  * cos(rt);
        }

        /* --- render: ceiling and floor --- */
        fill_hband(pixels, 0,            SCREEN_H / 2, rgb_pack(CEILING_COL));
        fill_hband(pixels, SCREEN_H / 2, SCREEN_H,     rgb_pack(FLOOR_COL));

        /* --- render: wall columns (DDA raycasting) --- */
        for (int x = 0; x < SCREEN_W; x++) {

            double cam_x = 2.0 * x / (double)SCREEN_W - 1.0;
            double raydx = drx + px * cam_x;
            double raydy = dry + py * cam_x;

            int mx = (int)xpos;
            int my = (int)ypos;

            double xdd = (raydx == 0.0) ? 1e30 : fabs(1.0 / raydx);
            double ydd = (raydy == 0.0) ? 1e30 : fabs(1.0 / raydy);

            double xsided, ysided;
            int xstep, ystep, side = 0;

            if (raydx < 0.0) { xstep = -1; xsided = (xpos - mx) * xdd; }
            else              { xstep =  1; xsided = (mx + 1.0 - xpos) * xdd; }
            if (raydy < 0.0) { ystep = -1; ysided = (ypos - my) * ydd; }
            else              { ystep =  1; ysided = (my + 1.0 - ypos) * ydd; }

            /* DDA grid traversal */
            bool hit = false;
            while (!hit) {
                if (xsided < ysided) {
                    xsided += xdd;
                    mx     += xstep;
                    side    = 0;
                } else {
                    ysided += ydd;
                    my     += ystep;
                    side    = 1;
                }
                /* bounds guard — prevents crash on malformed maps */
                if (mx < 0 || mx >= MAP_W || my < 0 || my >= MAP_H) break;
                if (map[mx][my] > 0) hit = true;
            }
            if (!hit) continue;

            /* perpendicular distance (fisheye-free) */
            double perpd = (side == 0) ? (xsided - xdd) : (ysided - ydd);
            if (perpd < 1e-4) perpd = 1e-4;

            /* projected wall-slice height */
            int lh = (int)(SCREEN_H / perpd);
            int ls = SCREEN_H / 2 - lh / 2;
            int le = ls + lh - 1;

            int wtype = map[mx][my];
            draw_vline(pixels, x, ls, le, wall_color(wtype, side, perpd));
        }

        /* --- blit pixel buffer to screen --- */
        SDL_UpdateTexture(tex, NULL, pixels, SCREEN_W * (int)sizeof(uint32_t));
        SDL_RenderClear(rend);
        SDL_RenderCopy(rend, tex, NULL, NULL);
        SDL_RenderPresent(rend);
    }

    /* --- cleanup --- */
    free(pixels);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(rend);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
