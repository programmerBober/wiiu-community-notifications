#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NOTIFY_STYLE_SPOTPASS_BLUE = 0,
    NOTIFY_STYLE_DARK_ELEGANT,
    NOTIFY_STYLE_WARNING_AMBER
} NotificationStyle;

/**
 * Connects to NotificationModule via libnotifications.
 * Must be called before pushing notifications to the screen overlay.
 */
bool Overlay_Init(void);

/**
 * Disconnects from NotificationModule and frees overlay client state.
 */
void Overlay_Shutdown(void);

/**
 * Checks if the rendering overlay frame is active and ready to display.
 */
bool Overlay_IsReady(void);

/**
 * Dispatches a formatted notification toast to the Wii U screen.
 * Persists until shown if console is transitioning applications.
 */
bool Overlay_ShowToast(const char* title, 
                       const char* body, 
                       float duration_sec, 
                       NotificationStyle style);

#ifdef __cplusplus
}
#endif
