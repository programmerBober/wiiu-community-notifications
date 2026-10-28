#include "notifications_overlay.h"
#include "notification_config.h"
#include <stdio.h>
#include <string.h>

#if defined(__WIIU__) || defined(__WUT__)
#include <notifications/notifications.h>
#endif

static bool s_overlay_initialized = false;

bool Overlay_Init(void) {
#if defined(__WIIU__) || defined(__WUT__)
    NotificationModuleStatus status = NotificationModule_InitLibrary();
    if (status != NOTIFICATION_MODULE_RESULT_SUCCESS && 
        status != NOTIFICATION_MODULE_RESULT_UNSUPPORTED_VERSION) {
        // Module might not be ready yet during early boot, can retry
        s_overlay_initialized = false;
        return false;
    }
    s_overlay_initialized = true;
    return true;
#else
    printf("[Overlay] Initialized mock screen toast overlay (libnotifications)\n");
    s_overlay_initialized = true;
    return true;
#endif
}

void Overlay_Shutdown(void) {
    if (!s_overlay_initialized) return;
#if defined(__WIIU__) || defined(__WUT__)
    NotificationModule_DeInitLibrary();
#else
    printf("[Overlay] Deinitialized screen toast overlay\n");
#endif
    s_overlay_initialized = false;
}

bool Overlay_IsReady(void) {
    if (!s_overlay_initialized) {
        if (!Overlay_Init()) return false;
    }
#if defined(__WIIU__) || defined(__WUT__)
    bool ready = false;
    NotificationModuleStatus status = NotificationModule_IsOverlayReady(&ready);
    return (status == NOTIFICATION_MODULE_RESULT_SUCCESS) && ready;
#else
    return true;
#endif
}

bool Overlay_ShowToast(const char* title, 
                       const char* body, 
                       float duration_sec, 
                       NotificationStyle style) {
    if (!s_overlay_initialized) {
        if (!Overlay_Init()) return false;
    }

    if (!title && !body) return false;

    char formatted[512];
    if (title && body && strlen(title) > 0) {
        snprintf(formatted, sizeof(formatted), "✦ %s ✦\n%s", title, body);
    } else if (title) {
        snprintf(formatted, sizeof(formatted), "✦ %s ✦", title);
    } else {
        snprintf(formatted, sizeof(formatted), "%s", body);
    }

#if defined(__WIIU__) || defined(__WUT__)
    NMColor textColor = { WIIU_TEXT_R, WIIU_TEXT_G, WIIU_TEXT_B, WIIU_TEXT_A };
    NMColor bgColor;

    switch (style) {
        case NOTIFY_STYLE_SPOTPASS_BLUE:
            bgColor = (NMColor){ WIIU_BLUE_R, WIIU_BLUE_G, WIIU_BLUE_B, WIIU_BLUE_A };
            break;
        case NOTIFY_STYLE_DARK_ELEGANT:
            bgColor = (NMColor){ WIIU_DARK_R, WIIU_DARK_G, WIIU_DARK_B, WIIU_DARK_A };
            break;
        case NOTIFY_STYLE_WARNING_AMBER:
            bgColor = (NMColor){ 230, 140, 20, 240 };
            break;
        case NOTIFY_STYLE_SUCCESS_GREEN:
            bgColor = (NMColor){ 40, 180, 70, 240 };
            break;
        case NOTIFY_STYLE_PARTY_PURPLE:
            bgColor = (NMColor){ 145, 60, 210, 240 };
            break;
        case NOTIFY_STYLE_CRITICAL_RED:
            bgColor = (NMColor){ 220, 45, 45, 245 };
            break;
        default:
            bgColor = (NMColor){ WIIU_BLUE_R, WIIU_BLUE_G, WIIU_BLUE_B, WIIU_BLUE_A };
            break;
    }

    if (duration_sec <= 0.0f) {
        duration_sec = 6.0f;
    }

    // keepUntilShown = true ensures the toast isn't dropped if game title is swapping
    NotificationModuleStatus status = NotificationModule_AddInfoNotificationEx(
        formatted,
        duration_sec,
        textColor,
        bgColor,
        nullptr,
        nullptr
    );

    return (status == NOTIFICATION_MODULE_RESULT_SUCCESS);
#else
    const char* color_ansi = "\033[1;36m"; // Cyan
    if (style == NOTIFY_STYLE_WARNING_AMBER) color_ansi = "\033[1;33m";      // Yellow
    else if (style == NOTIFY_STYLE_DARK_ELEGANT) color_ansi = "\033[1;34m";  // Blue
    else if (style == NOTIFY_STYLE_SUCCESS_GREEN) color_ansi = "\033[1;32m"; // Green
    else if (style == NOTIFY_STYLE_PARTY_PURPLE) color_ansi = "\033[1;35m";  // Magenta/Purple
    else if (style == NOTIFY_STYLE_CRITICAL_RED) color_ansi = "\033[1;31m";  // Red

    printf("\n%s┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓\033[0m\n", color_ansi);
    printf("%s┃  [Wii U Notification Toast]                         ┃\033[0m\n", color_ansi);
    printf("%s┃  Duration: %.1fs                                    ┃\033[0m\n", color_ansi, duration_sec);
    printf("%s┠─────────────────────────────────────────────────────┨\033[0m\n", color_ansi);
    
    // Print lines indented
    char temp[512];
    strncpy(temp, formatted, sizeof(temp));
    char* line = strtok(temp, "\n");
    while (line != NULL) {
        printf("%s┃\033[0m  %-51s%s┃\033[0m\n", color_ansi, line, color_ansi);
        line = strtok(NULL, "\n");
    }
    printf("%s┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛\033[0m\n\n", color_ansi);
    return true;
#endif
}
