
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspiofilemgr.h>
#include <png.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PSP_MODULE_INFO("RANDOM", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define SCREEN_W 480
#define SCREEN_H 272
#define BUF_W 512
#define SPRITE_COUNT 16
#define FLOOR_Y 210.0f

typedef struct {
    unsigned int *pixels;
    int width, height;
    int texWidth, texHeight;
} Sprite;

typedef struct {
    float u, v;
    float x, y, z;
} Vertex;

static unsigned int __attribute__((aligned(16))) displayList[262144];
static Sprite sprites[SPRITE_COUNT];

static const char *spriteNames[SPRITE_COUNT] = {
    "idle_1.png", "idle_2.png",
    "walk_1.png", "walk_2.png",
    "run_1.png", "run_2.png", "run_3.png", "run_4.png",
    "jump_1.png", "jump_2.png",
    "fall.png", "landing.png",
    "attack_1.png", "attack_2.png",
    "hurt.png", "magic.png"
};

enum {
    IDLE1, IDLE2, WALK1, WALK2,
    RUN1, RUN2, RUN3, RUN4,
    JUMP1, JUMP2, FALL, LANDING,
    ATTACK1, ATTACK2, HURT, MAGIC
};

static int mode = 0;
static int selection = 0;
static int previousButtons = 0;
static int frameCount = 0;
static int animation = IDLE1;
static int facingLeft = 0;
static int grounded = 1;
static int jumps = 0;
static int actionTimer = 0;
static int loadedSprites = 0;

static float playerX = 208.0f;
static float playerY = FLOOR_Y;
static float velocityY = 0.0f;

#define GRAVITY 0.38f
#define JUMP_SPEED -7.5f

static unsigned int nextPowerOfTwo(unsigned int n)
{
    unsigned int p = 1;
    while (p < n && p < 512)
        p <<= 1;
    return p;
}

/* Carrega PNG e converte para textura RGBA do PSP. */
static int loadPNG(const char *path, Sprite *out)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return 0;

    unsigned char signature[8];

    if (fread(signature, 1, 8, fp) != 8 ||
        png_sig_cmp(signature, 0, 8)) {
        fclose(fp);
        return 0;
    }

    png_structp png = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!png) {
        fclose(fp);
        return 0;
    }

    png_infop info = png_create_info_struct(png);

    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return 0;
    }

    unsigned char *raw = NULL;
    png_bytep *rows = NULL;
    unsigned int *texture = NULL;

    if (setjmp(png_jmpbuf(png))) {
        free(raw);
        free(rows);
        free(texture);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    png_init_io(png, fp);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    png_uint_32 w, h;
    int depth, type, interlace, compression, filter;

    png_get_IHDR(png, info, &w, &h, &depth, &type,
                 &interlace, &compression, &filter);

    if (!w || !h || w > 512 || h > 512) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    if (depth == 16)
        png_set_strip_16(png);

    if (type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);

    if (type == PNG_COLOR_TYPE_GRAY && depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);

    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);

    if (type == PNG_COLOR_TYPE_GRAY ||
        type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    if (!(type & PNG_COLOR_MASK_ALPHA) &&
        !png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);

    png_read_update_info(png, info);

    size_t rowBytes = png_get_rowbytes(png, info);

    raw = malloc(rowBytes * h);
    rows = malloc(sizeof(png_bytep) * h);

    if (!raw || !rows)
        png_error(png, "Memoria insuficiente");

    for (unsigned int y = 0; y < h; y++)
        rows[y] = raw + y * rowBytes;

    png_read_image(png, rows);

    unsigned int tw = nextPowerOfTwo(w);
    unsigned int th = nextPowerOfTwo(h);

    texture = memalign(16, tw * th * sizeof(unsigned int));

    if (!texture)
        png_error(png, "Textura sem memoria");

    memset(texture, 0, tw * th * sizeof(unsigned int));

    for (unsigned int y = 0; y < h; y++) {
        unsigned char *src = rows[y];

        for (unsigned int x = 0; x < w; x++) {
            unsigned char r = src[x * 4 + 0];
            unsigned char g = src[x * 4 + 1];
            unsigned char b = src[x * 4 + 2];
            unsigned char a = src[x * 4 + 3];

            texture[y * tw + x] =
                ((unsigned int)a << 24) |
                ((unsigned int)b << 16) |
                ((unsigned int)g << 8) |
                (unsigned int)r;
        }
    }

    out->pixels = texture;
    out->width = (int)w;
    out->height = (int)h;
    out->texWidth = (int)tw;
    out->texHeight = (int)th;

    free(raw);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    return 1;
}

static int loadSprites(void)
{
    const char *roots[] = {
        "ms0:/PSP/GAME/RANDOM/assets/sprites/",
        "ef0:/PSP/GAME/RANDOM/assets/sprites/",
        "assets/sprites/"
    };

    char path[256];
    int count = 0;

    for (int i = 0; i < SPRITE_COUNT; i++) {
        sprites[i].pixels = NULL;
        sprites[i].width = 0;
        sprites[i].height = 0;

        for (int r = 0; r < 3; r++) {
            snprintf(path, sizeof(path), "%s%s",
                     roots[r], spriteNames[i]);

            if (loadPNG(path, &sprites[i])) {
                count++;
                break;
            }
        }
    }

    return count;
}

static void initGraphics(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, displayList);

    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCREEN_W, SCREEN_H,
                    (void *)0x88000, BUF_W);

    sceGuOffset(2048 - SCREEN_W / 2,
                2048 - SCREEN_H / 2);
    sceGuViewport(2048, 2048, SCREEN_W, SCREEN_H);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);

    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_BLEND);

    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA,
                   GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

static void beginFrame(void)
{
    sceGuStart(GU_DIRECT, displayList);
    sceGuClearColor(0xFF090B10);
    sceGuClear(GU_COLOR_BUFFER_BIT);
}

static void endFrame(void)
{
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

static void drawRect(float x, float y, float w, float h,
                     unsigned int color)
{
    Vertex *v = sceGuGetMemory(2 * sizeof(Vertex));

    v[0].u = 0;
    v[0].v = 0;
    v[0].x = x;
    v[0].y = y;
    v[0].z = 0;

    v[1].u = 0;
    v[1].v = 0;
    v[1].x = x + w;
    v[1].y = y + h;
    v[1].z = 0;

    sceGuDisable(GU_TEXTURE_2D);
    sceGuColor(color);

    sceGuDrawArray(GU_SPRITES,
        GU_VERTEX_32BITF | GU_TRANSFORM_2D,
        2, NULL, v);
}

/* Fonte 5x7: A-Z, 0-9 e pontuacao basica. */
static const unsigned char font[42][5] = {
    {62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},
    {33,65,69,75,49},{24,20,18,127,16},{39,69,69,69,57},
    {60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},
    {6,73,73,41,30},
    {126,17,17,17,126},{127,73,73,73,54},
    {62,65,65,65,34},{127,65,65,34,28},
    {127,73,73,73,65},{127,9,9,9,1},
    {62,65,73,73,122},{127,8,8,8,127},
    {0,65,127,65,0},{32,64,65,63,1},
    {127,8,20,34,65},{127,64,64,64,64},
    {127,2,12,2,127},{127,4,8,16,127},
    {62,65,65,65,62},{127,9,9,9,6},
    {62,65,81,33,94},{127,9,25,41,70},
    {70,73,73,73,49},{1,1,127,1,1},
    {63,64,64,64,63},{31,32,64,32,31},
    {63,64,56,64,63},{99,20,8,20,99},
    {7,8,112,8,7},{97,81,73,69,67},
    {0,0,0,0,0},{0,54,54,0,0},{0,96,96,0,0},
    {8,8,8,8,8},{0,65,34,20,8},{8,20,34,65,0}
};

static int fontIndex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return 10 + c - 'A';
    if (c == ' ') return 36;
    if (c == ':') return 37;
    if (c == '.') return 38;
    if (c == '-') return 39;
    if (c == '>') return 40;
    if (c == '<') return 41;
    return 36;
}

static void drawText(int x, int y, const char *s,
                     unsigned int color, int scale)
{
    while (*s) {
        int index = fontIndex(*s++);

        for (int col = 0; col < 5; col++) {
            unsigned char bits = font[index][col];

            for (int row = 0; row < 7; row++) {
                if (bits & (1 << row))
                    drawRect(x + col * scale,
                             y + row * scale,
                             scale, scale, color);
            }
        }

        x += 6 * scale;
    }
}

static void drawSprite(int id, float x, float y, int flip)
{
    if (id < 0 || id >= SPRITE_COUNT)
        return;

    Sprite *s = &sprites[id];
    if (!s->pixels)
        return;

    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, GU_FALSE);
    sceGuTexImage(0, s->texWidth, s->texHeight,
                  s->texWidth, s->pixels);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuColor(0xFFFFFFFF);

    float u1 = flip ? (float)s->width : 0.0f;
    float u2 = flip ? 0.0f : (float)s->width;
    float v2 = (float)s->height;

    Vertex *v = sceGuGetMemory(2 * sizeof(Vertex));

    v[0].u = u1;
    v[0].v = 0;
    v[0].x = x;
    v[0].y = y;
    v[0].z = 0;

    v[1].u = u2;
    v[1].v = v2;
    v[1].x = x + s->width;
    v[1].y = y + s->height;
    v[1].z = 0;

    sceGuDrawArray(GU_SPRITES,
        GU_TEXTURE_32BITF | GU_VERTEX_32BITF |
        GU_TRANSFORM_2D, 2, NULL, v);

    sceGuDisable(GU_TEXTURE_2D);
}

static int direction(SceCtrlData *pad)
{
    int dy = (int)pad->Ly - 128;

    if (pad->Buttons & PSP_CTRL_UP) return -1;
    if (pad->Buttons & PSP_CTRL_DOWN) return 1;
    if (dy < -45) return -1;
    if (dy > 45) return 1;

    return 0;
}

static void drawMenu(void)
{
    beginFrame();

    drawText(174, 40, "RANDOM", 0xFFFFFFFF, 3);
    drawText(119, 76, "A JOURNEY BEGINS",
             0xFF9AAAC0, 1);

    drawRect(110, 108, 260, 1, 0xFF405070);

    drawText(143, 129, selection == 0 ?
             "> NEW GAME" : "  NEW GAME",
             selection == 0 ? 0xFF80D8FF : 0xFFFFFFFF, 2);

    drawText(143, 157, selection == 1 ?
             "> CONTINUE" : "  CONTINUE",
             selection == 1 ? 0xFF80D8FF : 0xFFFFFFFF, 2);

    drawText(143, 185, selection == 2 ?
             "> EXIT" : "  EXIT",
             selection == 2 ? 0xFF80D8FF : 0xFFFFFFFF, 2);

    drawText(65, 244, "ANALOG OR DPAD X SELECT",
             0xFF9AAAC0, 1);

    endFrame();
}

static void updatePlayer(SceCtrlData *pad)
{
    int dx = (int)pad->Lx - 128;
    int moving = 0;
    int running = (pad->Buttons & PSP_CTRL_RTRIGGER) != 0;
    float speed = running ? 3.5f : 2.0f;

    if (pad->Buttons & PSP_CTRL_LEFT) dx = -128;
    if (pad->Buttons & PSP_CTRL_RIGHT) dx = 127;

    if (dx < -25) {
        playerX -= speed;
        facingLeft = 1;
        moving = 1;
    } else if (dx > 25) {
        playerX += speed;
        facingLeft = 0;
        moving = 1;
    }

    if (playerX < 0) playerX = 0;
    if (playerX > SCREEN_W - 80)
        playerX = SCREEN_W - 80;

    if ((pad->Buttons & PSP_CTRL_CROSS) &&
        !(previousButtons & PSP_CTRL_CROSS) && jumps < 2) {
        velocityY = JUMP_SPEED;
        grounded = 0;
        jumps++;
    }

    if (!grounded) {
        velocityY += GRAVITY;
        playerY += velocityY;

        if (velocityY < 0)
            animation = jumps > 1 ? JUMP2 : JUMP1;
        else
            animation = FALL;

        if (playerY >= FLOOR_Y) {
            playerY = FLOOR_Y;
            velocityY = 0;
            grounded = 1;
            jumps = 0;
            animation = LANDING;
            actionTimer = 8;
        }
    } else if (actionTimer > 0) {
        actionTimer--;
    } else if (pad->Buttons & PSP_CTRL_SQUARE) {
        animation = ((frameCount / 8) & 1) ?
                    ATTACK2 : ATTACK1;
    } else if (pad->Buttons & PSP_CTRL_CIRCLE) {
        animation = MAGIC;
    } else if (moving && running) {
        animation = RUN1 + ((frameCount / 6) % 4);
    } else if (moving) {
        animation = WALK1 + ((frameCount / 10) % 2);
    } else {
        animation = ((frameCount / 25) & 1) ?
                    IDLE2 : IDLE1;
    }

    frameCount++;
}

static void drawGame(void)
{
    beginFrame();

    drawRect(0, FLOOR_Y + 64, SCREEN_W,
             SCREEN_H - FLOOR_Y - 64, 0xFF191B23);
    drawRect(0, FLOOR_Y + 62, SCREEN_W, 2, 0xFF667080);

    drawSprite(animation, playerX, playerY, facingLeft);

    drawText(8, 8, "RANDOM", 0xFFFFFFFF, 1);
    drawText(8, 22, "START PAUSE", 0xFFB0B8C8, 1);

    if (loadedSprites < SPRITE_COUNT)
        drawText(8, 38, "SPRITES MISSING", 0xFF8080FF, 1);

    endFrame();
}

static void drawPause(void)
{
    beginFrame();
    drawText(190, 72, "PAUSED", 0xFFFFFFFF, 2);
    drawText(132, 125, "> RESUME", 0xFF80D8FF, 2);
    drawText(106, 160, "START TO RETURN",
             0xFFB0B8C8, 1);
    endFrame();
}

int main(void)
{
    SceCtrlData pad;
    int previousDirection = 0;

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    initGraphics();
    loadedSprites = loadSprites();

    while (1) {
        sceCtrlReadBufferPositive(&pad, 1);

        int buttons = pad.Buttons;
        int confirm = (buttons & PSP_CTRL_CROSS) &&
                      !(previousButtons & PSP_CTRL_CROSS);
        int start = (buttons & PSP_CTRL_START) &&
                    !(previousButtons & PSP_CTRL_START);
        int dir = direction(&pad);

        if (mode == 0) {
            if (dir && !previousDirection) {
                selection += dir;
                if (selection < 0) selection = 2;
                if (selection > 2) selection = 0;
            }

            if (confirm) {
                if (selection == 0) {
                    playerX = 208;
                    playerY = FLOOR_Y;
                    velocityY = 0;
                    grounded = 1;
                    jumps = 0;
                    frameCount = 0;
                    mode = 1;
                } else if (selection == 1) {
                    /* Save ainda nao foi implementado. */
                } else {
                    break;
                }
            }

            drawMenu();
        } else if (mode == 1) {
            if (start) {
                mode = 2;
            } else {
                updatePlayer(&pad);
                drawGame();
            }
        } else {
            drawPause();
            if (start || confirm)
                mode = 1;
        }

        previousDirection = dir;
        previousButtons = buttons;
    }

    sceGuDisplay(GU_FALSE);
    sceGuTerm();

    for (int i = 0; i < SPRITE_COUNT; i++)
        free(sprites[i].pixels);

    sceKernelExitGame();
    return 0;
}
