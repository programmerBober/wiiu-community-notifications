#include <whb/proc.h>
#include <whb/gfx.h>
#include <sysapp/launch.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <vpad/input.h>
#include <notifications/notifications.h>
#include <nn/acp/drcled_c.h>
#include <nn/act/client_cpp.h>
#include <stdio.h>

int main(int argc, char **argv) {
    WHBProcInit();

    // 1. Initialize GX2 Graphics Pipeline
    // NotificationModule is a GX2 overlay! It requires GX2 frame swaps to draw toasts.
    if (!WHBGfxInit()) {
        SYSLaunchMenu();
        WHBProcShutdown();
        return 0;
    }

    // 2. Connect to Aroma NotificationModule
    NotificationModuleStatus notif_status = NotificationModule_InitLibrary();

    // 3. Turn on the iconic GamePad HOME button breathing LED
    uint32_t persistentId = nn::act::GetPersistentId();
    if (persistentId == 0) persistentId = 1;
    ACPTurnOnDrcLed(persistentId, 1);

    // 4. Dispatch the authentic Wii U Cyan Toast immediately on startup
    NMColor cyan_bg = { 0, 162, 232, 240 };
    NMColor white_fg = { 255, 255, 255, 255 };

    if (notif_status == NOTIFICATION_MODULE_RESULT_SUCCESS) {
        NotificationModule_AddInfoNotificationEx(
            "✦ Community Notification System ✦\nWii U SpotPass Revived! GamePad LED Glowing.",
            8.0f,
            white_fg,
            cyan_bg,
            nullptr,
            nullptr
        );
    }

    uint32_t count = 0;
    VPADStatus vpad;
    VPADReadError verror;

    while (WHBProcIsRunning()) {
        VPADRead(VPAD_CHAN_0, &vpad, 1, &verror);

        if (verror == VPAD_READ_SUCCESS) {
            // [A] Send Community Announcement Toast
            if (vpad.trigger & VPAD_BUTTON_A) {
                count++;
                char msg[256];
                snprintf(msg, sizeof(msg), "✦ Pretendo Splatfest #%u ✦\nInkopolis Plaza is active! Choose your side.", count);
                NotificationModule_AddInfoNotificationEx(
                    msg,
                    7.0f,
                    white_fg,
                    cyan_bg,
                    nullptr,
                    nullptr
                );
                ACPTurnOnDrcLed(persistentId, 1);
            }

            // [B] Send Error / Warning Shake Toast
            if (vpad.trigger & VPAD_BUTTON_B) {
                NotificationModule_AddErrorNotification("✦ Alert Test ✦\nWarning: Server maintenance in 15 mins.");
            }

            // [X] Turn ON GamePad HOME LED
            if (vpad.trigger & VPAD_BUTTON_X) {
                ACPTurnOnDrcLed(persistentId, 1);
            }

            // [Y] Turn OFF GamePad HOME LED
            if (vpad.trigger & VPAD_BUTTON_Y) {
                ACPTurnOffDrcLed();
            }

            // [+] or [-] or HOME triggers a clean transition back to the Wii U Menu
            if (vpad.trigger & (VPAD_BUTTON_PLUS | VPAD_BUTTON_MINUS | VPAD_BUTTON_HOME)) {
                SYSLaunchMenu();
                // WHBProcIsRunning() will return false on the next cycle
            }
        }

        // Render GX2 frame for both TV and GamePad
        // NotificationModule hooks into these swap buffers to render the toasts on top!
        WHBGfxBeginRender();

        // TV screen (Deep authentic Wii U Midnight Blue)
        WHBGfxBeginRenderTV();
        WHBGfxClearColor(0.06f, 0.10f, 0.16f, 1.0f);
        WHBGfxFinishRenderTV();

        // DRC GamePad screen
        WHBGfxBeginRenderDRC();
        WHBGfxClearColor(0.06f, 0.10f, 0.16f, 1.0f);
        WHBGfxFinishRenderDRC();

        // Finalize frame & swap scan buffers
        WHBGfxFinishRender();
    }

    // Clean exit
    ACPTurnOffDrcLed();
    NotificationModule_DeInitLibrary();
    WHBGfxShutdown();
    WHBProcShutdown();
    return 0;
}
