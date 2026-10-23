
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>

#include <stdio.h>

PSP_MODULE_INFO("RANDOM", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define MENU_NOVO       0
#define MENU_CONTINUAR  1
#define MENU_SAIR       2
#define MENU_TOTAL      3

static int menuSelecionado = MENU_NOVO;

/* Evita repetir a acao enquanto o botao continua pressionado */
static int botaoAnterior = 0;

/* Le o direcional e o analogico */
static int lerVertical(SceCtrlData *pad)
{
    int analogico = (int)pad->Ly - 128;

    if (pad->Buttons & PSP_CTRL_UP)
        return -1;

    if (pad->Buttons & PSP_CTRL_DOWN)
        return 1;

    if (analogico < -45)
        return -1;

    if (analogico > 45)
        return 1;

    return 0;
}

static void desenharMenu(void)
{
    pspDebugScreenClear();

    pspDebugScreenSetTextColor(0xFFFFFFFF);
    pspDebugScreenSetXY(15, 3);
    pspDebugScreenPrintf("R A N D O M");

    pspDebugScreenSetTextColor(0xFFAAAAAA);
    pspDebugScreenSetXY(8, 5);
    pspDebugScreenPrintf("UMA JORNADA PELO DESCONHECIDO");

    pspDebugScreenSetTextColor(0xFFFFFFFF);
    pspDebugScreenSetXY(10, 9);

    if (menuSelecionado == MENU_NOVO)
        pspDebugScreenPrintf("> Novo Jogo");
    else
        pspDebugScreenPrintf("  Novo Jogo");

    pspDebugScreenSetXY(10, 11);

    if (menuSelecionado == MENU_CONTINUAR)
        pspDebugScreenPrintf("> Continuar");
    else
        pspDebugScreenPrintf("  Continuar");

    pspDebugScreenSetXY(10, 13);

    if (menuSelecionado == MENU_SAIR)
        pspDebugScreenPrintf("> Sair");
    else
        pspDebugScreenPrintf("  Sair");

    pspDebugScreenSetTextColor(0xFFAAAAAA);
    pspDebugScreenSetXY(4, 19);
    pspDebugScreenPrintf("ANALOGICO / DIRECIONAL: MOVER");

    pspDebugScreenSetXY(4, 21);
    pspDebugScreenPrintf("X: CONFIRMAR");

    pspDebugScreenSetXY(4, 23);
    pspDebugScreenPrintf("RANDOM PSP - VERSAO DE TESTE");
}

static void mostrarMensagem(const char *titulo,
                            const char *linha1,
                            const char *linha2)
{
    pspDebugScreenClear();

    pspDebugScreenSetTextColor(0xFFFFFFFF);
    pspDebugScreenSetXY(5, 5);
    pspDebugScreenPrintf("RANDOM PSP");

    pspDebugScreenSetXY(5, 9);
    pspDebugScreenPrintf("%s", titulo);

    pspDebugScreenSetTextColor(0xFFAAAAAA);
    pspDebugScreenSetXY(5, 12);
    pspDebugScreenPrintf("%s", linha1);

    pspDebugScreenSetXY(5, 13);
    pspDebugScreenPrintf("%s", linha2);

    pspDebugScreenSetTextColor(0xFFFFFFFF);
    pspDebugScreenSetXY(5, 19);
    pspDebugScreenPrintf("CIRCULO: VOLTAR AO MENU");

    while (1)
    {
        SceCtrlData pad;
        sceCtrlReadBufferPositive(&pad, 1);

        if (pad.Buttons & PSP_CTRL_CIRCLE)
            break;

        sceDisplayWaitVblankStart();
    }

    /* Espera o botao ser solto */
    while (1)
    {
        SceCtrlData pad;
        sceCtrlReadBufferPositive(&pad, 1);

        if (!(pad.Buttons & PSP_CTRL_CIRCLE))
            break;

        sceDisplayWaitVblankStart();
    }
}

int main(void)
{
    SceCtrlData pad;
    int direcaoAnterior = 0;

    pspDebugScreenInit();

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    while (1)
    {
        int direcao;
        int confirmar;
        int botoes;

        sceCtrlReadBufferPositive(&pad, 1);

        botoes = pad.Buttons;
        direcao = lerVertical(&pad);

        /* Move uma opcao por movimento */
        if (direcao != 0 && direcaoAnterior == 0)
        {
            menuSelecionado += direcao;

            if (menuSelecionado < 0)
                menuSelecionado = MENU_TOTAL - 1;

            if (menuSelecionado >= MENU_TOTAL)
                menuSelecionado = 0;
        }

        direcaoAnterior = direcao;

        /* X so ativa uma vez por pressionamento */
        confirmar = (botoes & PSP_CTRL_CROSS) &&
                    !(botaoAnterior & PSP_CTRL_CROSS);

        botaoAnterior = botoes;

        if (confirmar)
        {
            if (menuSelecionado == MENU_NOVO)
            {
                mostrarMensagem(
                    "NOVO JOGO",
                    "Menu funcionando corretamente.",
                    "Proxima etapa: personagem e mapa."
                );
            }
            else if (menuSelecionado == MENU_CONTINUAR)
            {
                mostrarMensagem(
                    "CONTINUAR",
                    "Sistema de salvamento ainda",
                    "sera implementado."
                );
            }
            else if (menuSelecionado == MENU_SAIR)
            {
                break;
            }

            /* Evita confirmar novamente ao voltar */
            botaoAnterior = PSP_CTRL_CROSS;
            direcaoAnterior = 0;
        }

        desenharMenu();
        sceDisplayWaitVblankStart();
    }

    sceKernelExitGame();
    return 0;
}
