#include "drc_led_controller.h"
#include "notification_config.h"
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
    nn::act::Initialize();
    s_persistent_id = nn::act::GetPersistentId();
    if (s_persistent_id == 0) {
        for (uint8_t s = 1; s <= 12; s++) {
            uint32_t pid = nn::act::GetPersistentIdEx(s);
            if (pid != 0) {
                s_persistent_id = pid;
                break;
            }
        }
    }
    if (s_persistent_id == 0) {
        s_persistent_id = 0x80000001;
    }
    nn::act::Finalize();
    s_led_initialized = true;
    return true;
#else
    s_persistent_id = 0x80000001;
    s_led_initialized = true;
    printf("[DrcLed] Initialized mock GamePad LED controller (PersistentID: 0x%08X)\n", s_persistent_id);
    return true;
#endif
}

void DrcLed_Shutdown(void) {
    if (!s_led_initialized) return;
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
