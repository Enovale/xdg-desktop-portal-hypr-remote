#pragma once

extern "C" {
#include <libei.h>
}

class WaylandVirtualKeyboard;
class WaylandVirtualPointer;

class LibEIHandler {
public:
    LibEIHandler();
    ~LibEIHandler();
    
    bool init(WaylandVirtualKeyboard* kb, WaylandVirtualPointer* ptr);
    void cleanup();
    void run();
    void stop();
    
    // Public access to ei_context for portal integration
    struct ei* ei_context;
    
    // Public access to virtual input devices for portal integration
    WaylandVirtualKeyboard* keyboard;
    WaylandVirtualPointer* pointer;
    
private:
    struct ei_seat* seat;
    
    bool running;
    
    void handle_event(struct ei_event* event);
    void handle_keyboard_event(struct ei_event* event);
    void handle_pointer_event(struct ei_event* event);
}; 