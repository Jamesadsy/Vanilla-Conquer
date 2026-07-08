//
// Copyright 2020 Electronic Arts Inc.
//
// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free
// software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.

// TiberianDawn.DLL and RedAlert.dll and corresponding source code is distributed
// in the hope that it will be useful, but with permitted additional restrictions
// under Section 7 of the GPL. See the GNU General Public License in LICENSE.TXT
// distributed with this program. You should have received a copy of the
// GNU General Public License along with permitted additional restrictions
// with this program. If not, see https://github.com/electronicarts/CnC_Remastered_Collection

#include "macros.h"
#include "wwkeyboard_sdl2.h"
#include "video.h"
#include "sdl_keymap.h"
#include "settings.h"
#include <cmath>
#include <SDL.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

void Focus_Loss();
void Focus_Restore();
void Process_Network();

// Defined (non-static) in video_sdl2.cpp. Maps a normalized (0..1) finger position
// to the engine's 640x400 cursor space via render_dst, writing hwcursor.X/Y and
// latching touch mode so Get_Video_Mouse reports that position.
void Set_Touch_Position(float nx, float ny);

/*
** Touch diagnostics. Sideloaded apps get no os_log; append gesture decisions to a
** plain text file visible in Files (On My iPhone -> VanillaTD -> vctouch.txt).
** Kept separate from vcdbg.txt so the boot log stays readable. No-op off iOS.
** Only transitions/clicks are logged (never per-motion), so the file stays small.
*/
#if defined(__APPLE__) && TARGET_OS_IOS
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <ctime>
static void TOUCHLOG_write(const char* fmt, ...)
{
    const char* home = getenv("HOME");
    if (home == nullptr) {
        home = ".";
    }
    char path[1200];
    snprintf(path, sizeof(path), "%s/Documents/vctouch.txt", home);
    FILE* f = fopen(path, "a");
    if (f == nullptr) {
        return;
    }
    time_t t = time(nullptr);
    struct tm* lt = localtime(&t);
    char ts[32];
    if (lt) {
        strftime(ts, sizeof(ts), "%H:%M:%S", lt);
        fprintf(f, "[%s] ", ts);
    }
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fflush(f);
    fclose(f);
}
#define TOUCHLOG(fmt, ...) TOUCHLOG_write(fmt, ##__VA_ARGS__)
#else
#define TOUCHLOG(fmt, ...) ((void)0)
#endif

WWKeyboardClassSDL2::~WWKeyboardClassSDL2()
{
}

void WWKeyboardClassSDL2::Fill_Buffer_From_System(void)
{
#ifdef NETWORKING
    Process_Network();
#endif
    SDL_Event event;

    while (!Is_Buffer_Full() && SDL_PollEvent(&event)) {
        unsigned short key;
        switch (event.type) {
        case SDL_QUIT:
            exit(0);
            break;
        case SDL_KEYDOWN:
#if defined(__APPLE__) && TARGET_OS_IOS
            // Double-char fix: on iOS the soft keyboard emits BOTH an SDL_KEYDOWN (real
            // scancode) AND an SDL_TEXTINPUT for each printable key. The scancode route
            // (To_ASCII + sdl_keymap) and the SDL_TEXTINPUT route (WWKEY_TEXT_BIT) would
            // each insert the char into an edit box -> doubling (ABC -> AABBCC). While text
            // input is active (an edit box is focused), drop the scancode for any key that
            // produces a printable char so only SDL_TEXTINPUT inserts. SDL_TEXTINPUT is the
            // authoritative source (correct casing/shift/symbols). Control keys
            // (Return/Backspace/Tab/Esc -> < 0x20; arrows/F-keys/modifiers -> KA_NONE) fall
            // through unchanged. The guard is inert outside edit boxes (text input off), so
            // gameplay hotkeys and control-groups are unaffected.
            if (SDL_IsTextInputActive()) {
                KeyASCIIType ascii = To_ASCII(event.key.keysym.scancode);
                if (ascii >= KA_SPACE && ascii <= 0x7E) {
                    TOUCHLOG("keydown-drop: printable scancode %u ('%c') suppressed (TEXTINPUT owns it)",
                             (unsigned)event.key.keysym.scancode,
                             (char)ascii);
                    break;
                }
            }
#endif
            Put_Key_Message(event.key.keysym.scancode, false);
            break;
#if defined(__APPLE__) && TARGET_OS_IOS
        case SDL_TEXTINPUT:
            // iOS soft keyboard delivers typed characters here as UTF-8 text, not as
            // scancodes. Inject each printable ASCII char into the keyboard buffer as a
            // literal-text key (WWKEY_TEXT_BIT); To_ASCII returns the low byte directly,
            // so EditClass inserts it. Return/Backspace still arrive via SDL_KEYDOWN
            // scancodes and are handled by the normal keymap path, so they are skipped here.
            for (const char* p = event.text.text; *p != '\0'; ++p) {
                unsigned char c = (unsigned char)*p;
                if (c >= 0x20 && c < 0x7F) {
                    Put((unsigned short)(c | WWKEY_TEXT_BIT));
                    TOUCHLOG("textinput: '%c' (0x%02X) -> buffer", c, c);
                }
            }
            break;
#endif
        case SDL_KEYUP:
            if (event.key.keysym.scancode == SDL_SCANCODE_RETURN && Down(VK_MENU)) {
                Toggle_Video_Fullscreen();
            } else {
#if defined(__APPLE__) && TARGET_OS_IOS
                // Symmetric with SDL_KEYDOWN: while text input is active, drop the release
                // of a printable scancode too, so the scancode route contributes nothing
                // for printables (SDL_TEXTINPUT owns them). Control-key releases fall through.
                if (SDL_IsTextInputActive()) {
                    KeyASCIIType ascii = To_ASCII(event.key.keysym.scancode);
                    if (ascii >= KA_SPACE && ascii <= 0x7E) {
                        break;
                    }
                }
#endif
                Put_Key_Message(event.key.keysym.scancode, true);
            }
            break;
        case SDL_MOUSEMOTION:
            // Ignore mouse motion synthesised from touches (we handle SDL_FINGER* directly).
            if (event.motion.which == SDL_TOUCH_MOUSEID) {
                break;
            }
            Move_Video_Mouse(static_cast<float>(event.motion.xrel), static_cast<float>(event.motion.yrel));
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            // Ignore mouse buttons synthesised from touches.
            if (event.button.which == SDL_TOUCH_MOUSEID) {
                break;
            }

            int x, y;

            switch (event.button.button) {
            case SDL_BUTTON_LEFT:
            default:
                key = VK_LBUTTON;
                break;
            case SDL_BUTTON_RIGHT:
                key = VK_RBUTTON;
                break;
            case SDL_BUTTON_MIDDLE:
                key = VK_MBUTTON;
                break;
            }

            if (Settings.Mouse.RawInput || Is_Gamepad_Active()) {
                Get_Video_Mouse(x, y);
            } else {
                float scale_x = 1.0f, scale_y = 1.0f;
                Get_Video_Scale(scale_x, scale_y);
                x = event.button.x / scale_x;
                y = event.button.y / scale_y;
            }

            Put_Mouse_Message(key, x, y, event.type == SDL_MOUSEBUTTONDOWN ? false : true);
        } break;
        case SDL_FINGERDOWN:
            Handle_Touch_Down(event.tfinger);
            break;
        case SDL_FINGERMOTION:
            Handle_Touch_Motion(event.tfinger);
            break;
        case SDL_FINGERUP:
            Handle_Touch_Up(event.tfinger);
            break;
        case SDL_WINDOWEVENT:
            switch (event.window.event) {
            case SDL_WINDOWEVENT_EXPOSED:
            case SDL_WINDOWEVENT_RESTORED:
            case SDL_WINDOWEVENT_FOCUS_GAINED:
                Focus_Restore();
                break;
            case SDL_WINDOWEVENT_HIDDEN:
            case SDL_WINDOWEVENT_MINIMIZED:
            case SDL_WINDOWEVENT_FOCUS_LOST:
                Focus_Loss();
                break;
            }
            break;
        case SDL_MOUSEWHEEL:
            if (event.wheel.y > 0) { // scroll up
                Put_Key_Message(VK_MOUSEWHEEL_UP, false);
            } else if (event.wheel.y < 0) { // scroll down
                Put_Key_Message(VK_MOUSEWHEEL_DOWN, false);
            }
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (GameController != nullptr) {
                const SDL_GameController* removedController = SDL_GameControllerFromInstanceID(event.jdevice.which);
                if (removedController == GameController) {
                    SDL_GameControllerClose(GameController);
                    GameController = nullptr;
                }
            }
            break;
        case SDL_CONTROLLERDEVICEADDED:
            if (GameController == nullptr) {
                GameController = SDL_GameControllerOpen(event.jdevice.which);
            }
            break;
        case SDL_CONTROLLERAXISMOTION:
            Handle_Controller_Axis_Event(event.caxis);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            Handle_Controller_Button_Event(event.cbutton);
            break;
        }
    }
    if (Is_Gamepad_Active()) {
        Process_Controller_Axis_Motion();
    }

    // Poll for touch long-press (a stationary finger emits no events).
    Process_Touch_Poll();
}

void WWKeyboardClassSDL2::Handle_Touch_Down(const SDL_TouchFingerEvent& finger)
{
    TouchDeviceId = finger.touchId;
    int nfingers = SDL_GetNumTouchFingers(finger.touchId);

    if (nfingers >= 2) {
        // A second finger arrived -> map-pan mode. If a left-drag was in progress,
        // release the button first so it can't get stuck down.
        if (TouchLeftDown) {
            int gx, gy;
            Get_Video_Mouse(gx, gy);
            Put_Mouse_Message(VK_LBUTTON, gx, gy, true);
            TouchLeftDown = false;
        }

        SDL_Finger* f0 = SDL_GetTouchFinger(finger.touchId, 0);
        SDL_Finger* f1 = SDL_GetTouchFinger(finger.touchId, 1);
        if (f0 != nullptr && f1 != nullptr) {
            PanStartCx = (f0->x + f1->x) * 0.5f;
            PanStartCy = (f0->y + f1->y) * 0.5f;
        }
        TouchState = TOUCH_PANNING;
        AnalogScrollActive = false;
        ScrollDirection = SDIR_NONE;
        TOUCHLOG("down: 2 fingers -> PANNING (centroid %.3f,%.3f)", PanStartCx, PanStartCy);
        return;
    }

    // First finger: record, move cursor there, but emit NOTHING yet.
    TouchPrimaryFinger = finger.fingerId;
    TouchStartNormX = finger.x;
    TouchStartNormY = finger.y;
    TouchStartTime = SDL_GetTicks();
    TouchLeftDown = false;

    Set_Touch_Position(finger.x, finger.y);
    Get_Video_Mouse(TouchStartGameX, TouchStartGameY);
    TouchState = TOUCH_PENDING;

    TOUCHLOG("down: norm=(%.4f,%.4f) game=(%d,%d) -> PENDING",
             finger.x,
             finger.y,
             TouchStartGameX,
             TouchStartGameY);
}

void WWKeyboardClassSDL2::Handle_Touch_Motion(const SDL_TouchFingerEvent& finger)
{
    if (TouchState == TOUCH_PANNING) {
        int n = SDL_GetNumTouchFingers(finger.touchId);
        if (n < 2) {
            return;
        }
        SDL_Finger* f0 = SDL_GetTouchFinger(finger.touchId, 0);
        SDL_Finger* f1 = SDL_GetTouchFinger(finger.touchId, 1);
        if (f0 == nullptr || f1 == nullptr) {
            return;
        }
        float cx = (f0->x + f1->x) * 0.5f;
        float cy = (f0->y + f1->y) * 0.5f;
        float dx = cx - PanStartCx;
        float dy = cy - PanStartCy;

        ScrollDirType dirX = SDIR_NONE;
        ScrollDirType dirY = SDIR_NONE;
// Content-drag (natural): world follows the fingers; camera scrolls
        // opposite to finger travel.
        if (dx > TOUCH_PAN_THRESHOLD) {
            dirX = SDIR_W;
        } else if (dx < -TOUCH_PAN_THRESHOLD) {
            dirX = SDIR_E;
        }
        if (dy > TOUCH_PAN_THRESHOLD) {
            dirY = SDIR_N;
        } else if (dy < -TOUCH_PAN_THRESHOLD) {
            dirY = SDIR_S;
        }

        if (dirX == SDIR_E && dirY == SDIR_N) {
            ScrollDirection = SDIR_NE;
            AnalogScrollActive = true;
        } else if (dirX == SDIR_E && dirY == SDIR_S) {
            ScrollDirection = SDIR_SE;
            AnalogScrollActive = true;
        } else if (dirX == SDIR_W && dirY == SDIR_N) {
            ScrollDirection = SDIR_NW;
            AnalogScrollActive = true;
        } else if (dirX == SDIR_W && dirY == SDIR_S) {
            ScrollDirection = SDIR_SW;
            AnalogScrollActive = true;
        } else if (dirX == SDIR_E) {
            ScrollDirection = SDIR_E;
            AnalogScrollActive = true;
        } else if (dirX == SDIR_W) {
            ScrollDirection = SDIR_W;
            AnalogScrollActive = true;
        } else if (dirY == SDIR_S) {
            ScrollDirection = SDIR_S;
            AnalogScrollActive = true;
        } else if (dirY == SDIR_N) {
            ScrollDirection = SDIR_N;
            AnalogScrollActive = true;
        } else {
            ScrollDirection = SDIR_NONE;
            AnalogScrollActive = false;
        }
        return;
    }

    if (finger.fingerId != TouchPrimaryFinger) {
        return;
    }

    if (TouchState == TOUCH_PENDING) {
        float dx = finger.x - TouchStartNormX;
        float dy = finger.y - TouchStartNormY;
        if ((dx * dx + dy * dy) > (TOUCH_DEADZONE * TOUCH_DEADZONE)) {
            // Crossed the deadzone -> begin a left-drag (box select), anchored at
            // the original press point. Send button-down at the anchor, then move
            // the cursor to the current finger so the selection box grows.
            Set_Touch_Position(TouchStartNormX, TouchStartNormY);
            Put_Mouse_Message(VK_LBUTTON, TouchStartGameX, TouchStartGameY, false);
            TouchLeftDown = true;
            Set_Touch_Position(finger.x, finger.y);
            TouchState = TOUCH_DRAGGING;
            TOUCHLOG("motion: deadzone crossed -> DRAGGING (anchor %d,%d)",
                     TouchStartGameX,
                     TouchStartGameY);
        } else {
            // Still a potential tap: track cursor without emitting anything.
            Set_Touch_Position(finger.x, finger.y);
        }
    } else if (TouchState == TOUCH_DRAGGING) {
        Set_Touch_Position(finger.x, finger.y);
    }
}

void WWKeyboardClassSDL2::Handle_Touch_Up(const SDL_TouchFingerEvent& finger)
{
    if (TouchState == TOUCH_PANNING) {
        AnalogScrollActive = false;
        ScrollDirection = SDIR_NONE;
        int n = SDL_GetNumTouchFingers(finger.touchId);
        // n still counts the finger being lifted on some SDL versions; <=1 means done.
        if (n <= 1) {
            TouchState = TOUCH_IDLE;
        }
        TOUCHLOG("up: PANNING lift (remaining~%d)", n);
        return;
    }

    if (finger.fingerId != TouchPrimaryFinger) {
        return;
    }

    if (TouchState == TOUCH_PENDING) {
        // Clean tap -> left click at the press point (down+up same coords).
        Set_Touch_Position(TouchStartNormX, TouchStartNormY);
        int gx, gy;
        Get_Video_Mouse(gx, gy);
        Put_Mouse_Message(VK_LBUTTON, gx, gy, false);
        Put_Mouse_Message(VK_LBUTTON, gx, gy, true);
        TOUCHLOG("up: TAP -> Lclick (%d,%d)", gx, gy);
    } else if (TouchState == TOUCH_DRAGGING) {
        Set_Touch_Position(finger.x, finger.y);
        int gx, gy;
        Get_Video_Mouse(gx, gy);
        Put_Mouse_Message(VK_LBUTTON, gx, gy, true);
        TouchLeftDown = false;
        TOUCHLOG("up: DRAG end -> Lup (%d,%d)", gx, gy);
    } else if (TouchState == TOUCH_LONGPRESSED) {
        TOUCHLOG("up: after LONGPRESS");
    }

    TouchState = TOUCH_IDLE;
}

void WWKeyboardClassSDL2::Process_Touch_Poll()
{
    if (TouchState == TOUCH_PENDING) {
        uint32_t now = SDL_GetTicks();
        if (now - TouchStartTime >= TOUCH_LONGPRESS_MS) {
            // Long-press -> right click (deselect / cancel) at the press point.
            Set_Touch_Position(TouchStartNormX, TouchStartNormY);
            Put_Mouse_Message(VK_RBUTTON, TouchStartGameX, TouchStartGameY, false);
            Put_Mouse_Message(VK_RBUTTON, TouchStartGameX, TouchStartGameY, true);
            TouchState = TOUCH_LONGPRESSED;
            TOUCHLOG("poll: LONGPRESS -> Rclick (%d,%d)", TouchStartGameX, TouchStartGameY);
        }
    }
}

bool WWKeyboardClassSDL2::Is_Gamepad_Active()
{
    return GameController != nullptr;
}

void WWKeyboardClassSDL2::Open_Controller()
{
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            GameController = SDL_GameControllerOpen(i);
        }
    }
}

void WWKeyboardClassSDL2::Close_Controller()
{
    if (SDL_GameControllerGetAttached(GameController)) {
        SDL_GameControllerClose(GameController);
        GameController = nullptr;
    }
}

void WWKeyboardClassSDL2::Process_Controller_Axis_Motion()
{
    const uint32_t currentTime = SDL_GetTicks();
    const float deltaTime = currentTime - LastControllerTime;
    LastControllerTime = currentTime;

    if (ControllerLeftXAxis != 0 || ControllerLeftYAxis != 0) {
        const int16_t xSign = (ControllerLeftXAxis > 0) - (ControllerLeftXAxis < 0);
        const int16_t ySign = (ControllerLeftYAxis > 0) - (ControllerLeftYAxis < 0);

        float movX = std::pow(std::abs(ControllerLeftXAxis), CONTROLLER_AXIS_SPEEDUP) * xSign * deltaTime
                     * Settings.Mouse.ControllerPointerSpeed / CONTROLLER_SPEED_MOD * ControllerSpeedBoost;
        float movY = std::pow(std::abs(ControllerLeftYAxis), CONTROLLER_AXIS_SPEEDUP) * ySign * deltaTime
                     * Settings.Mouse.ControllerPointerSpeed / CONTROLLER_SPEED_MOD * ControllerSpeedBoost;

        Move_Video_Mouse(movX, movY);
    }
}

void WWKeyboardClassSDL2::Handle_Controller_Axis_Event(const SDL_ControllerAxisEvent& motion)
{
    AnalogScrollActive = false;
    ScrollDirType directionX = SDIR_NONE;
    ScrollDirType directionY = SDIR_NONE;

    if (motion.axis == SDL_CONTROLLER_AXIS_LEFTX) {
        if (std::abs(motion.value) > CONTROLLER_L_DEADZONE)
            ControllerLeftXAxis = motion.value;
        else
            ControllerLeftXAxis = 0;
    } else if (motion.axis == SDL_CONTROLLER_AXIS_LEFTY) {
        if (std::abs(motion.value) > CONTROLLER_L_DEADZONE)
            ControllerLeftYAxis = motion.value;
        else
            ControllerLeftYAxis = 0;
    } else if (motion.axis == SDL_CONTROLLER_AXIS_RIGHTX) {
        if (std::abs(motion.value) > CONTROLLER_R_DEADZONE)
            ControllerRightXAxis = motion.value;
        else
            ControllerRightXAxis = 0;
    } else if (motion.axis == SDL_CONTROLLER_AXIS_RIGHTY) {
        if (std::abs(motion.value) > CONTROLLER_R_DEADZONE)
            ControllerRightYAxis = motion.value;
        else
            ControllerRightYAxis = 0;
    } else if (motion.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
        if (std::abs(motion.value) > CONTROLLER_TRIGGER_R_DEADZONE)
            ControllerSpeedBoost = 1 + (static_cast<float>(motion.value) / 32767) * CONTROLLER_TRIGGER_SPEEDUP;
        else
            ControllerSpeedBoost = 1;
    }

    if (ControllerRightXAxis != 0) {
        AnalogScrollActive = true;
        directionX = ControllerRightXAxis > 0 ? SDIR_E : SDIR_W;
    }
    if (ControllerRightYAxis != 0) {
        AnalogScrollActive = true;
        directionY = ControllerRightYAxis > 0 ? SDIR_S : SDIR_N;
    }

    if (directionX == SDIR_E && directionY == SDIR_N) {
        ScrollDirection = SDIR_NE;
    } else if (directionX == SDIR_E && directionY == SDIR_S) {
        ScrollDirection = SDIR_SE;
    } else if (directionX == SDIR_W && directionY == SDIR_N) {
        ScrollDirection = SDIR_NW;
    } else if (directionX == SDIR_W && directionY == SDIR_S) {
        ScrollDirection = SDIR_SW;
    } else if (directionX == SDIR_E) {
        ScrollDirection = SDIR_E;
    } else if (directionX == SDIR_W) {
        ScrollDirection = SDIR_W;
    } else if (directionY == SDIR_S) {
        ScrollDirection = SDIR_S;
    } else if (directionY == SDIR_N) {
        ScrollDirection = SDIR_N;
    }
}

void WWKeyboardClassSDL2::Handle_Controller_Button_Event(const SDL_ControllerButtonEvent& button)
{
    bool keyboardPress = false;
    bool mousePress = false;
    unsigned short key;
    SDL_Scancode scancode;

    switch (button.button) {
    case SDL_CONTROLLER_BUTTON_A:
        mousePress = true;
        key = VK_LBUTTON;
        break;
    case SDL_CONTROLLER_BUTTON_B:
        mousePress = true;
        key = VK_RBUTTON;
        break;
    case SDL_CONTROLLER_BUTTON_X:
        keyboardPress = true;
        scancode = SDL_SCANCODE_G;
        break;
    case SDL_CONTROLLER_BUTTON_Y:
        keyboardPress = true;
        scancode = SDL_SCANCODE_F;
        break;
    case SDL_CONTROLLER_BUTTON_BACK:
        keyboardPress = true;
        scancode = SDL_SCANCODE_ESCAPE;
        break;
    case SDL_CONTROLLER_BUTTON_START:
        keyboardPress = true;
        scancode = SDL_SCANCODE_RETURN;
        break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        keyboardPress = true;
        scancode = SDL_SCANCODE_LCTRL;
        break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        keyboardPress = true;
        scancode = SDL_SCANCODE_LALT;
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        keyboardPress = true;
        scancode = SDL_SCANCODE_1;
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        keyboardPress = true;
        scancode = SDL_SCANCODE_2;
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        keyboardPress = true;
        scancode = SDL_SCANCODE_3;
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        keyboardPress = true;
        scancode = SDL_SCANCODE_4;
        break;
    default:
        break;
    }

    if (keyboardPress) {
        Put_Key_Message(scancode, button.state == SDL_RELEASED);
    } else if (mousePress) {
        int x, y;
        Get_Video_Mouse(x, y);
        Put_Mouse_Message(key, x, y, button.state == SDL_RELEASED);
    }
}

bool WWKeyboardClassSDL2::Is_Analog_Scroll_Active()
{
    return AnalogScrollActive;
}

unsigned char WWKeyboardClassSDL2::Get_Scroll_Direction()
{
    return ScrollDirection;
}

KeyASCIIType WWKeyboardClassSDL2::To_ASCII(unsigned short key)
{
    if (key & WWKEY_RLS_BIT) {
        return KA_NONE;
    }

    // Literal typed character injected from SDL_TEXTINPUT (iOS soft keyboard). The low
    // byte is already the ASCII value, so return it directly and skip the scancode keymap.
    if (key & WWKEY_TEXT_BIT) {
        return (KeyASCIIType)(key & 0xFF);
    }

    key &= 0xFF; // drop all mods

    if (key > ARRAY_SIZE(sdl_keymap) / 2 - 1) {
        return KA_NONE;
    }

    if (SDL_GetModState() & KMOD_SHIFT) {
        return sdl_keymap[key + ARRAY_SIZE(sdl_keymap) / 2];
    } else {
        return sdl_keymap[key];
    }
}

void WWKeyboardClassSDL2::Show_Soft_Keyboard()
{
#if defined(__APPLE__) && TARGET_OS_IOS
    if (!SDL_IsTextInputActive()) {
        SDL_StartTextInput();
        TOUCHLOG("soft keyboard: SDL_StartTextInput (edit focus gained)");
    }
#endif
}

void WWKeyboardClassSDL2::Hide_Soft_Keyboard()
{
#if defined(__APPLE__) && TARGET_OS_IOS
    if (SDL_IsTextInputActive()) {
        SDL_StopTextInput();
        TOUCHLOG("soft keyboard: SDL_StopTextInput (edit focus cleared)");
    }
#endif
}

WWKeyboardClass* CreateWWKeyboardClass(void)
{
    return new WWKeyboardClassSDL2;
}
