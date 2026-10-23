
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspiofilemgr.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

PSP_MODULE_INFO("RANDOM", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define SW 480
#define SH 272
#define BW 512
#define MAX_SPRITES 16
#define GRAVITY 0.38f
#define JUMP_SPEED -7.5f
#define FLOOR_Y 218.0f

typedef struct {
    unsigned int *pixels;
    int w, h;
} Sprite;

static unsigned int __attribute__((aligned(16))) displayList[262144];
static Sprite sprites[MAX_SPRITES];

static const char *spriteNames[MAX_SPRITES] = {
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

static int screenMode = 0; /* 0 menu, 1 jogo, 2 pausa */
static int menuSelection = 0;
static int previousButtons = 0;

static float playerX = 208.0f;
static float playerY = FLOOR_Y;
static float velocityY = 0.0f;
static float playerSpeed = 2.0f;
static int grounded = 1;
static int facing = 1;
static int jumps = 0;
static int animation = IDLE1;
static int animationTick = 0;
static int actionTimer = 0;
static int saveExists = 0;

/* PNG: converte a imagem para pixels RGBA de 32 bits. */
static int loadPNG(const char *path, Sprite *out)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

    unsigned char sig[8];
    if (fread(sig, 1, 8, fp) != 8 ||
        png_sig_cmp(sig, 0, 8)) {
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

    if (setjmp(png_jmpbuf(png))) {
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

    if (w == 0 || h == 0 || w > 512 || h > 512) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    if (depth == 16) png_set_strip_16(png);
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
    unsigned char *raw = malloc(rowBytes * h);
    png_bytep *rows = malloc(sizeof(png_bytep) * h);

    if (!raw || !rows) {
        free(raw);
        free(rows);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    for (unsigned int y = 0; y < h; y++)
        rows[y] = raw + y * rowBytes;

    png_read_image(png, rows);

    unsigned int *rgba = memalign(16, w * h * 4);
    if (!rgba) {
        free(raw);
        free(rows);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return 0;
    }

    for (unsigned int y = 0; y < h; y++) {
        unsigned char *src = rows[y];
        for (unsigned int x = 0; x < w; x++) {
            unsigned char r = src[x * 4 + 0];
            unsigned char g = src[x * 4 + 1];
            unsigned char b = src[x * 4 + 2];
            unsigned char a = src[x * 4 + 3];

            /* Formato de pixel para a textura GU. */
            rgba[y * w + x] =
                ((unsigned int)a << 24) |
                ((unsigned int)b << 16) |
                ((unsigned int)g << 8) | r;
        }
    }

    out->pixels = rgba;
    out->w = w;
    out->h = h;

    free(raw);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return 1;
}

static int loadSpriteSet(void)
{
    char path[256];
    const char *roots[] = {
        "ms0:/PSP/GAME/RANDOM/assets/sprites/",
        "ef0:/PSP/GAME/RANDOM/assets/sprites/",
        "assets/sprites/"
    };

    int loaded = 0;

    for (int i = 0; i < MAX_SPRITES; i++) {
        sprites[i].pixels = NULL;
        sprites[i].w = sprites[i].h = 0;

        for (int r = 0; r < 3; r++) {
            snprintf(path, sizeof(path), "%s%s",
                     roots[r], spriteNames[i]);

            if (loadPNG(path, &sprites[i])) {
                loaded++;
                break;
            }
        }
    }

    return loaded;
}

typedef struct {
    float x, y;
    float u, v;
} Vertex;

static void drawSprite(int id, float x, float y, int flip)
{
    if (id < 0 || id >= MAX_SPRITES) return;
    Sprite *s = &sprites[id];
    if (!s->pixels) return;

    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, GU_FALSE);
    sceGuTexImage(0, s->w, s->h, s->w, s->pixels);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuColor(0xFFFFFFFF);

    float x2 = x + s->w;
    float u1 = flip ? 1.0f : 0.0f;
    float u2 = flip ? 0.0f : 1.0f;

    Vertex *v = (Vertex *)sceGuGetMemory(2 * sizeof(Vertex));

    v[0].u = u1;
    v[0].v = 0;
    v[0].x = x;
    v[0].y = y;
    v[0].z = 0;

    v[1].u = u2;
    v[1].v = 1;
    v[1].x = x2;
    v[1].y = y + s->h;
    v[1].z = 0;

    sceGuDrawArray(GU_SPRITES,
        GU_TEXTURE_32BITF | GU_VERTEX_32BITF |
        GU_TRANSFORM_2D, 2, NULL, v);

    sceGuDisable(GU_TEXTURE_2D);
}

static void drawRect(float x, float y, float w, float h,
                     unsigned int color)
{
    typedef struct { float x, y, z; } V;
    V *v = (V *)sceGuGetMemory(2 * sizeof(V));

    v[0].x = x;
    v[0].y = y;
    v[0].z = 0;
    v[1].x = x + w;
    v[1].y = y + h;
    v[1].z = 0;

    sceGuDisable(GU_TEXTURE_2D);
    sceGuColor(color);
    sceGuDrawArray(GU_SPRITES,
        GU_VERTEX_32BITF | GU_TRANSFORM_2D,
        2, NULL, v);
}

/* Fonte 5x7: caracteres usados pelo menu e HUD. */
static const unsigned char font[][5] = {
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x3E,0x41,0x49,0x49,0x7A}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x3F,0x40,0x38,0x40,0x3F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x07,0x08,0x70,0x08,0x07}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
    {0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x41,0x22,0x14,0x08}, /* > */
    {0x08,0x14,0x22,0x41,0x00}  /* < */
};

static int charIndex(char c)
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

static void drawText(int x, int y, const char *str,
                     unsigned int color, int scale)
{
    sceGuDisable(GU_TEXTURE_2D);
    sceGuColor(color);

    while (*str) {
        int ci = charIndex(*str++);
        for (int col = 0; col < 5; col++) {
            unsigned char bits = font[ci][col];
            for (int row = 0; row < 7; row++) {
                if (bits & (1 << row))
                    drawRect(x + col * scale, y + row * scale,
                             scale, scale, color);
            }
        }
        x += 6 * scale;
    }
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

static void initGraphics(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, displayList);

    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BW);
    sceGuDispBuffer(SW, SH, (void *)0x88000, BW);
    sceGuOffset(2048 - SW / 2, 2048 - SH / 2);
    sceGuViewport(2048, 2048, SW, SH);
    sceGuScissor(0, 0, SW, SH);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_SMOOTH);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA,
                   GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuEnable(GU_BLEND);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

static void drawMenu(void)
{
    beginFrame();

    drawText(154, 42, "RANDOM", 0xFFFFFFFF, 3);
    drawText(104, 78, "A JOURNEY BEGINS", 0xFF9AAAC0, 1);

    drawRect(115, 112, 250, 1, 0xFF405070);

    drawText(145, 132,
        menuSelection == 0 ? "> NEW GAME" : "  NEW GAME",
        menuSelection == 0 ? 0xFF80D8FF : 0xFFFFFFFF, 2);

    drawText(145, 160,
        menuSelection == 1 ? "> CONTINUE" : "  CONTINUE",
        menuSelection == 1 ? 0xFF80D8FF : 0xFFFFFFFF, 2);

    drawText(145, 188,
        menuSelection == 2 ? "> EXIT" : "  EXIT",
        menuSelection == 2 ? 0xFF80D8FF : 0xFFFFFFFF, 2);

    drawText(68, 245, "ANALOG OR DPAD - X SELECT", 0xFF9AAAC0, 1);
    endFrame();
}

static int getDirection(SceCtrlData *pad)
{
    int dy = (int)pad->Ly - 128;

    if (pad->Buttons & PSP_CTRL_UP) return -1;
    if (pad->Buttons & PSP_CTRL_DOWN) return 1;
    if (dy < -45) return -1;
    if (dy > 45) return 1;

    return 0;
}

static void drawPause(void)
{
    beginFrame();
    drawText(190, 72, "PAUSED", 0xFFFFFFFF, 2);
    drawText(135, 125, "> RESUME", 0xFF80D8FF, 2);
    drawText(103, 160, "START TO RETURN", 0xFFB0B8C8, 1);
    endFrame();
}

static void updatePlayer(SceCtrlData *pad)
{
    int dx = (int)pad->Lx - 128;
    int moving = 0;
    int running = (pad->Buttons & PSP_CTRL_RTRIGGER) != 0;
    float speed = running ? 3.5f : playerSpeed;

    if (pad->Buttons & PSP_CTRL_LEFT) dx = -128;
    if (pad->Buttons & PSP_CTRL_RIGHT) dx = 127;

    if (dx < -25) {
        playerX -= speed;
        facing = 1;
        moving = 1;
    } else if (dx > 25) {
        playerX += speed;
        facing = 0;
        moving = 1;
    }

    if (playerX < 0) playerX = 0;
    if (playerX > SW - 80) playerX = SW - 80;

    if ((pad->Buttons & PSP_CTRL_CROSS) &&
        !(previousButtons & PSP_CTRL_CROSS)) {
        if (jumps < 2) {
            velocityY = JUMP_SPEED;
            grounded = 0;
            jumps++;
        }
    }

    if (!grounded) {
        velocityY += GRAVITY;
        playerY += velocityY;

        if (velocityY < 0)
            animation = (jumps > 1) ? JUMP2 : JUMP1;
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
        animation = (animationTick / 8) % 2 ? ATTACK2 : ATTACK1;
    } else if (pad->Buttons & PSP_CTRL_CIRCLE) {
        animation = MAGIC;
    } else if (moving && running) {
        animation = RUN1 + ((animationTick / 6) % 4);
    } else if (moving) {
        animation = WALK1 + ((animationTick / 10) % 2);
    } else {
        animation = ((animationTick / 25) % 2) ? IDLE2 : IDLE1;
    }

    animationTick++;
}

static void drawGame(void)
{
    beginFrame();

    drawRect(0, FLOOR_Y + 64, SW, SH - FLOOR_Y - 64,
             0xFF191B23);
    drawRect(0, FLOOR_Y + 62, SW, 2, 0xFF667080);

    drawSprite(animation, playerX, playerY, facing);

    drawText(8, 8, "RANDOM", 0xFFFFFFFF, 1);
    drawText(8, 24, "START PAUSE", 0xFFB0B8C8, 1);

    endFrame();
}

static int loadSave(void)
{
    SceUID fd = sceIoOpen(
        "ms0:/PSP/GAME/RANDOM/save.dat",
        PSP_O_RDONLY, 0);

    if (fd >= 0) {
        sceIoClose(fd);
        return 1;
    }

    return 0;
}

int main(void)
{
    SceCtrlData pad;
    int directionPrevious = 0;
    int loadedCount;

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    initGraphics();
    loadedCount = loadSpriteSet();
    saveExists = loadSave();

    (void)loadedCount;

    while (1) {
        sceCtrlReadBufferPositive(&pad, 1);
        int buttons = pad.Buttons;
        int confirm = (buttons & PSP_CTRL_CROSS) &&
                      !(previousButtons & PSP_CTRL_CROSS);
        int start = (buttons & PSP_CTRL_START) &&
                    !(previousButtons & PSP_CTRL_START);
        int direction = getDirection(&pad);

        if (screenMode == 0) {
            if (direction && !directionPrevious) {
                menuSelection += direction;
                if (menuSelection < 0) menuSelection = 2;
                if (menuSelection > 2) menuSelection = 0;
            }

            if (confirm) {
                if (menuSelection == 0) {
                    playerX = 208;
                    playerY = FLOOR_Y;
                    velocityY = 0;
                    grounded = 1;
                    jumps = 0;
                    animationTick = 0;
                    screenMode = 1;
                } else if (menuSelection == 1) {
                    if (saveExists) {
                        screenMode = 1;
                    } else {
                        /* Nenhum save real implementado ainda. */
                        menuSelection = 1;
                    }
                } else {
                    break;
                }
            }

            drawMenu();
        } else if (screenMode == 1) {
            if (start) {
                screenMode = 2;
            } else {
                updatePlayer(&pad);
                drawGame();
            }
        } else {
            drawPause();
            if (start || confirm)
                screenMode = 1;
        }

        directionPrevious = direction;
        previousButtons = buttons;
    }

    sceGuDisplay(GU_FALSE);
    sceGuTerm();

    for (int i = 0; i < MAX_SPRITES; i++)
        free(sprites[i].pixels);

    sceKernelExitGame();
    return 0;
}
