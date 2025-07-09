#pragma once

#include <sdbus-c++/sdbus-c++.h>
#include <memory>

extern "C" {
#include "libei-1.0/libeis.h"
}

class LibEIHandler;

class Portal {
public:
    Portal();
    ~Portal();
    
    bool init(LibEIHandler* handler);
    void cleanup();
    void run();
    void stop();
    
private:
    std::unique_ptr<sdbus::IConnection> connection;
    std::unique_ptr<sdbus::IObject> object;
    LibEIHandler* libei_handler;
    bool running;
    
    // Modifier state tracking for proper key combination handling
    uint32_t modifier_state_depressed = 0;
    uint32_t modifier_state_latched = 0;
    uint32_t modifier_state_locked = 0;
    uint32_t modifier_state_group = 0;
    
    // XKB modifier masks for common modifiers
    static constexpr uint32_t MOD_SHIFT = 1 << 0;
    static constexpr uint32_t MOD_CAPS = 1 << 1;
    static constexpr uint32_t MOD_CTRL = 1 << 2;
    static constexpr uint32_t MOD_ALT = 1 << 3;
    static constexpr uint32_t MOD_NUM = 1 << 4;
    static constexpr uint32_t MOD_META = 1 << 6; // Super/Windows key
    
    void update_modifier_state(uint32_t keycode, bool is_press);
    
    // D-Bus method handlers
    void CreateSession(sdbus::MethodCall call);
    void SelectSources(sdbus::MethodCall call);
    void SelectDevices(sdbus::MethodCall call);
    void Start(sdbus::MethodCall call);
    
    // Input notification methods - these are called by remote clients to send input events
    void NotifyPointerMotion(sdbus::MethodCall call);
    void NotifyPointerButton(sdbus::MethodCall call);
    void NotifyKeyboardKeycode(sdbus::MethodCall call);
    void NotifyKeyboardKeysym(sdbus::MethodCall call);
    void NotifyPointerAxis(sdbus::MethodCall call);
    
    // Modern EIS (Emulated Input Server) method
    void ConnectToEIS(sdbus::MethodCall call);
    
    // EIS event handling
    void handle_eis_event(struct eis_event* event);
};  