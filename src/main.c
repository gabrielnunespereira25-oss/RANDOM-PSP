
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <stdlib.h>

PSP_MODULE_INFO("RANDOM", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCREEN_W 480
#define SCREEN_H 272
#define BUF_W 512

static unsigned int __attribute__((aligned(16))) list[262144];

int main(void)
{
    pspDebugScreenInit();

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCREEN_W, SCREEN_H, (void *)0x88000, BUF_W);
    sceGuOffset(2048 - (SCREEN_W / 2), 2048 - (SCREEN_H / 2));
    sceGuViewport(2048, 2048, SCREEN_W, SCREEN_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    while (1) {
        SceCtrlData pad;
        sceCtrlReadBufferPositive(&pad, 1);

        sceGuStart(GU_DIRECT, list);
        sceGuClearColor(0xFF24180C);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        sceGuFinish();
        sceGuSync(0, 0);

        pspDebugScreenSetXY(12, 7);
        pspDebugScreenSetTextColor(0xFFFFFFFF);
        pspDebugScreenPrintf("RANDOM PSP");
        pspDebugScreenSetXY(8, 10);
        pspDebugScreenPrintf("PARTE 1 - TESTE DE INICIALIZACAO");
        pspDebugScreenSetXY(8, 13);
        pspDebugScreenPrintf("D-PAD: mover selecao");
        pspDebugScreenSetXY(8, 15);
        pspDebugScreenPrintf("X: confirmar");
        pspDebugScreenSetXY(8, 18);
        pspDebugScreenPrintf("START: sair");

        sceDisplayWaitVblankStart();

        if (pad.Buttons & PSP_CTRL_START) {
            break;
        }
    }

    sceGuDisplay(GU_FALSE);
    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
