#include <pspkernel.h>
#include <pspctrl.h>
#include <pspctrl_kernel.h>
#include <stdint.h>

PSP_MODULE_INFO("PSPRA_HomeCtrl", PSP_MODULE_KERNEL, 1, 0);

static volatile int g_prx_running = 1;
static volatile uint32_t *g_user_btn_ptr = NULL;
static SceUID g_thid = -1;

static int home_poll_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;

    SceCtrlData pad;
    while (g_prx_running) {
        if (g_user_btn_ptr != NULL) {
            if (sceCtrlPeekBufferPositive(&pad, 1) >= 0) {
                *g_user_btn_ptr = pad.Buttons;
            }
        }
        sceKernelDelayThread(10000);
    }
    return 0;
}

int module_start(SceSize args, void *argp) {
    if (args >= sizeof(void *) && argp != NULL) {
        g_user_btn_ptr = *(volatile uint32_t * const *)argp;
    }

    g_prx_running = 1;
    g_thid = sceKernelCreateThread("pspra_home_poll", home_poll_thread, 0x18, 0x800, 0, NULL);
    if (g_thid >= 0) {
        sceKernelStartThread(g_thid, 0, NULL);
    }
    return 0;
}

int module_stop(SceSize args, void *argp) {
    (void)args;
    (void)argp;

    g_prx_running = 0;
    if (g_thid >= 0) {
        sceKernelWaitThreadEnd(g_thid, NULL);
        sceKernelDeleteThread(g_thid);
        g_thid = -1;
    }
    return 0;
}
