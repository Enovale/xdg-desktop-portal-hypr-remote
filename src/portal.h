#pragma once

#include <sdbus-c++/sdbus-c++.h>
#include <memory>

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
    
    // D-Bus method handlers
    void CreateSession(sdbus::MethodCall call);
    void SelectSources(sdbus::MethodCall call);
    void SelectDevices(sdbus::MethodCall call);
    void Start(sdbus::MethodCall call);
    
    // Input notification methods - these are called by remote clients to send input events
    void NotifyPointerMotion(sdbus::MethodCall call);
    void NotifyPointerButton(sdbus::MethodCall call);
    void NotifyKeyboardKeycode(sdbus::MethodCall call);
    void NotifyPointerAxis(sdbus::MethodCall call);
    
    // Modern EIS (Emulated Input Server) method
    void ConnectToEIS(sdbus::MethodCall call);
}; 