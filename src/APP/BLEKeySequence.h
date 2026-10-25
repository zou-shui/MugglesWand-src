#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr size_t BLE_KEY_SEQUENCE_MAX = 128;
constexpr uint16_t BLE_KEY_DEFAULT_HOLD_MS = 25;
constexpr uint16_t BLE_KEY_DEFAULT_DELAY_MS = 25;
constexpr uint16_t BLE_KEY_TIMING_MAX_MS = 10000;

struct BLEKeyAction
{
    uint8_t modifiers;
    uint8_t keycode;
    uint16_t hold_ms;
    uint16_t delay_ms;
};

using BLEKeySequenceCallback = bool (*)(const BLEKeyAction &action, void *context);

// Parse and optionally execute a sequence in order.
// A null callback performs validation only.
bool BLEKeySequence_parse(const char *sequence,
                          BLEKeySequenceCallback callback,
                          void *context);
