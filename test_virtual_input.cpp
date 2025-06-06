#include "src/wayland_virtual_pointer.h"
#include "src/wayland_virtual_keyboard.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>

extern "C" {
#include <linux/input.h>
}

int main() {
    std::cout << "Testing Wayland Virtual Input..." << std::endl;
    
    WaylandVirtualPointer pointer;
    if (!pointer.init()) {
        std::cerr << "Failed to initialize virtual pointer" << std::endl;
        return 1;
    }
    
    std::cout << "✓ Virtual pointer initialized" << std::endl;
    std::cout << "Moving mouse in a small circle..." << std::endl;
    
    // Get current time
    uint32_t time = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    
    // Move mouse in a circle
    for (int i = 0; i < 360; i += 10) {
        double angle = i * M_PI / 180.0;
        double dx = cos(angle) * 2.0;
        double dy = sin(angle) * 2.0;
        
        pointer.send_motion(time + i, dx, dy);
        pointer.send_frame();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    std::cout << "✓ Mouse movement test completed" << std::endl;
    std::cout << "Testing mouse click..." << std::endl;
    
    // Test a left click
    pointer.send_button(time + 1000, BTN_LEFT, 1); // Press
    pointer.send_frame();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    pointer.send_button(time + 1100, BTN_LEFT, 0); // Release
    pointer.send_frame();
    
    std::cout << "✓ Mouse click test completed" << std::endl;
    
    pointer.cleanup();
    return 0;
} 