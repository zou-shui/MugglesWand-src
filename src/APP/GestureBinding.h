#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr size_t GESTURE_APP_NAME_SIZE = 16;
constexpr size_t GESTURE_PARAMETER_SIZE = 128;

// An empty app means the gesture has no binding.
struct GestureBinding
{
    char app[GESTURE_APP_NAME_SIZE];
    char parameter[GESTURE_PARAMETER_SIZE];
};

// Load the persistent mapping table and initialize the built-in defaults.
void APP_binding_init(void);

// Execute the app currently bound to a gesture ID.
void APP_binding_execute(int32_t gesture);

// Handle the raw argument string following the top-level "map" command.
void APP_binding_console(const char *args);
