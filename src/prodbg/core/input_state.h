#pragma once

#include "core/math.h"
#include "pd_keys.h"
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum MouseButton
{
    MouseButton_Left,
    MouseButton_Middle,
    MouseButton_Right,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct InputState
{
    PDMouseWheelEvent scrollEvent; // scrollEvent / gesture

    Vec2 mousePos;// position within the window
    Vec2 mouseScreenPos; // position on the screen

	uint32_t modifierFlags;
    uint32_t modifiers;
    bool mouseDown[16]; // mouse button states
    bool keysDown[512]; // Keyboard keys that are pressed
    float keyDownDuration[512];
    float deltaTime;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void InputState_update(float deltaTime);

int InputState_isKeyDown(int key, uint32_t modifiers, int repeat);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

InputState* InputState_getState();

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline bool Input_isLmbDown(const InputState* state)
{
    return state->mouseDown[MouseButton_Left];
}
