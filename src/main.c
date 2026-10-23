
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>

PSP_MODULE_INFO("RANDOM", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

int main(void)
{
    SceCtrlData pad;

    /* Inicializa o sistema de texto do PSP */
    pspDebugScreenInit();

    /* Configura os controles */
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);

    /* Limpa a tela e desenha o teste */
    pspDebugScreenClear();
    pspDebugScreenSetTextColor(0xFFFFFFFF);

    pspDebugScreenSetXY(10, 4);
    pspDebugScreenPrintf("RANDOM PSP");

    pspDebugScreenSetXY(5, 7);
    pspDebugScreenPrintf("TESTE DE INICIALIZACAO");

    pspDebugScreenSetXY(5, 10);
    pspDebugScreenPrintf("Se estas letras aparecem,");

    pspDebugScreenSetXY(5, 11);
    pspDebugScreenPrintf("o sistema de texto funciona.");

    pspDebugScreenSetXY(5, 15);
    pspDebugScreenPrintf("START: sair");

    /* Mantem o jogo aberto */
    while (1)
    {
        sceCtrlReadBufferPositive(&pad, 1);

        if (pad.Buttons & PSP_CTRL_START)
        {
            break;
        }

        sceDisplayWaitVblankStart();
    }

    sceKernelExitGame();
    return 0;
}
