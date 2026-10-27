#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initializes DRC LED subsystem.
 * Uses nn::acp and nn::act on Wii U Cafe OS.
 */
bool DrcLed_Init(void);

/**
 * Shuts down LED subsystem, ensuring the GamePad LED is turned off.
 */
void DrcLed_Shutdown(void);

/**
 * Turns on the GamePad HOME button LED with the specified pattern.
 * Pattern 1 is the default notification breathing / pulse on Wii U.
 */
void DrcLed_TurnOn(uint32_t pattern);

/**
 * Immediately extinguishes the GamePad HOME button LED.
 */
void DrcLed_TurnOff(void);

/**
 * Triggers a pulse/breathing notification cycle for a set duration in milliseconds.
 * Non-blocking if executed from background worker thread.
 */
void DrcLed_PulseNotification(uint32_t duration_ms);

#ifdef __cplusplus
}
#endif
