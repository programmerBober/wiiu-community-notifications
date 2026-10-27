#include "drc_led_controller.h"
#include <stdio.h>

#if defined(__WIIU__) || defined(__WUT__)
#include <nn/acp/drcled_c.h>
#include <nn/act/client_cpp.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#else
#include <unistd.h>
#endif

static bool s_led_initialized = false;
static uint32_t s_persistent_id = 0;

bool DrcLed_Init(void) {
#if defined(__WIIU__) || defined(__WUT__)
    s_persistent_id = nn::act::GetPersistentId();
    if (s_persistent_id == 0) {
        // Fallback default slot if no active account slot is bound
        s_persistent_id = 1;
    }
    s_led_initialized = true;
    return true;
#else
    s_persistent_id = 0x12345678;
    s_led_initialized = true;
    printf("[DrcLed] Initialized mock GamePad LED controller (PersistentID: 0x%08X)\n", s_persistent_id);
    return true;
#endif
}

void DrcLed_Shutdown(void) {
    if (!s_led_initialized) return;
    DrcLed_TurnOff();
    s_led_initialized = false;
}

void DrcLed_TurnOn(uint32_t pattern) {
    if (!s_led_initialized) DrcLed_Init();

#if defined(__WIIU__) || defined(__WUT__)
    ACPTurnOnDrcLed(s_persistent_id, (ACPDrcLedPattern)pattern);
#else
    printf("[DrcLed] >>> GamePad HOME Button LED ON (Pattern %u) <<<\n", pattern);
#endif
}

void DrcLed_TurnOff(void) {
#if defined(__WIIU__) || defined(__WUT__)
    ACPTurnOffDrcLed();
#else
    printf("[DrcLed] GamePad HOME Button LED OFF\n");
#endif
}

void DrcLed_PulseNotification(uint32_t duration_ms) {
    if (!s_led_initialized) DrcLed_Init();

    // Pattern 1: Iconic notification breathing pulse
    DrcLed_TurnOn(1);

#if defined(__WIIU__) || defined(__WUT__)
    OSSleepTicks(OSMillisecondsToTicks(duration_ms));
#else
    usleep(duration_ms * 1000);
#endif

    DrcLed_TurnOff();
}
