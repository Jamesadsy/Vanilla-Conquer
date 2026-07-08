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

#pragma once
#include "wwkeyboard.h"
#include <SDL.h>

class WWKeyboardClassSDL2 : public WWKeyboardClass
{
public:
    virtual ~WWKeyboardClassSDL2();

    virtual void Fill_Buffer_From_System(void);
    virtual bool Is_Gamepad_Active();
    virtual void Open_Controller();
    virtual void Close_Controller();
    virtual bool Is_Analog_Scroll_Active();
    virtual unsigned char Get_Scroll_Direction();
    virtual KeyASCIIType To_ASCII(unsigned short key);
    virtual void Show_Soft_Keyboard();
    virtual void Hide_Soft_Keyboard();

private:
    void Handle_Controller_Axis_Event(const SDL_ControllerAxisEvent& motion);
    void Handle_Controller_Button_Event(const SDL_ControllerButtonEvent& button);
    void Process_Controller_Axis_Motion();

    // Touch gesture handling (iOS/phone/tablet). Raw SDL_FINGER* events are mapped
    // to the engine's absolute cursor + mouse-button pipeline. See the .cpp for the
    // deferred-tap state machine (nothing is emitted on finger-down).
    void Handle_Touch_Down(const SDL_TouchFingerEvent& finger);
    void Handle_Touch_Motion(const SDL_TouchFingerEvent& finger);
    void Handle_Touch_Up(const SDL_TouchFingerEvent& finger);
    void Process_Touch_Poll();

    // used to convert user-friendly pointer speed values into more useable ones
    static constexpr float CONTROLLER_SPEED_MOD = 2000000.0f;
    // bigger value correndsponds to faster pointer movement speed with bigger stick axis values
    static constexpr float CONTROLLER_AXIS_SPEEDUP = 1.03f;
    // speedup value while the trigger is pressed
    static constexpr int CONTROLLER_TRIGGER_SPEEDUP = 2;

    enum
    {
        CONTROLLER_L_DEADZONE = 4000,
        CONTROLLER_R_DEADZONE = 6000,
        CONTROLLER_TRIGGER_R_DEADZONE = 3000
    };

    // Touch tuning constants.
    static constexpr uint32_t TOUCH_LONGPRESS_MS = 500;  // hold time -> right click
    static constexpr float TOUCH_DEADZONE = 0.02f;       // normalized move before a tap becomes a drag
    static constexpr float TOUCH_PAN_THRESHOLD = 0.03f;  // normalized 2-finger displacement before scrolling

    enum TouchStateType
    {
        TOUCH_IDLE,
        TOUCH_PENDING,     // one finger down, not yet classified
        TOUCH_DRAGGING,    // one finger, moved past deadzone -> left-drag select
        TOUCH_PANNING,     // two fingers -> map scroll
        TOUCH_LONGPRESSED  // long-press fired (right click already sent)
    };

    SDL_GameController* GameController = nullptr;
    int16_t ControllerLeftXAxis = 0;
    int16_t ControllerLeftYAxis = 0;
    int16_t ControllerRightXAxis = 0;
    int16_t ControllerRightYAxis = 0;
    uint32_t LastControllerTime = 0;
    float ControllerSpeedBoost = 1;
    bool AnalogScrollActive = false;
    ScrollDirType ScrollDirection = SDIR_NONE;

    // Touch gesture state.
    TouchStateType TouchState = TOUCH_IDLE;
    SDL_FingerID TouchPrimaryFinger = 0;
    SDL_TouchID TouchDeviceId = 0;
    float TouchStartNormX = 0.0f;
    float TouchStartNormY = 0.0f;
    int TouchStartGameX = 0;
    int TouchStartGameY = 0;
    uint32_t TouchStartTime = 0;
    bool TouchLeftDown = false;
    float PanStartCx = 0.0f;
    float PanStartCy = 0.0f;
};
