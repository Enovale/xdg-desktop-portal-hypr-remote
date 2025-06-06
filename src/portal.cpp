#include "portal.h"
#include "libei_handler.h"
#include "wayland_virtual_keyboard.h"
#include "wayland_virtual_pointer.h"
#include <iostream>
#include <thread>
#include <chrono>

extern "C" {
#include <libei.h>
#include <wayland-client-protocol.h>
}

static const char* PORTAL_INTERFACE = "org.freedesktop.impl.portal.RemoteDesktop";
static const char* PORTAL_PATH = "/org/freedesktop/portal/desktop";

// Use development name if requested, otherwise use standard name
static const char* PORTAL_NAME = "org.freedesktop.impl.portal.desktop.hyprland-remote-desktop";

Portal::Portal() : libei_handler(nullptr), running(false) {
}

Portal::~Portal() {
    cleanup();
}

bool Portal::init(LibEIHandler* handler) {
    libei_handler = handler;
    
    try {
        // Create D-Bus connection to SESSION bus (not system bus)
        connection = sdbus::createSessionBusConnection();
        
        // Request the portal name
        connection->requestName(PORTAL_NAME);
        
        // Create the portal object
        object = sdbus::createObject(*connection, PORTAL_PATH);
        std::cout << "Portal D-Bus interface registered at " << PORTAL_NAME << std::endl;
        std::cout << "Portal registered on SESSION bus (not system bus)" << std::endl;
        std::cout << "Portal version: 2" << std::endl;
        std::cout << "Portal path: " << PORTAL_PATH << std::endl;
        std::cout << "Portal interface: " << PORTAL_INTERFACE << std::endl;

        // Register RemoteDesktop interface methods with correct signatures
        object->registerMethod(PORTAL_INTERFACE, "CreateSession", "oosa{sv}", "ua{sv}", 
                              [this](sdbus::MethodCall call) { CreateSession(std::move(call)); });
        
        /* object->registerMethod(PORTAL_INTERFACE, "SelectSources", "oosa{sv}", "ua{sv}", 
                              [this](sdbus::MethodCall call) { SelectSources(std::move(call)); }); */
        
        object->registerMethod(PORTAL_INTERFACE, "SelectDevices", "oosa{sv}", "ua{sv}", 
                              [this](sdbus::MethodCall call) { SelectDevices(std::move(call)); });
        
        object->registerMethod(PORTAL_INTERFACE, "Start", "oossa{sv}", "ua{sv}", 
                              [this](sdbus::MethodCall call) { Start(std::move(call)); });
        
        // Add the input notification methods that clients call to send input events
        object->registerMethod(PORTAL_INTERFACE, "NotifyPointerMotion", "oa{sv}dd", "", 
                              [this](sdbus::MethodCall call) { NotifyPointerMotion(std::move(call)); });
        
        object->registerMethod(PORTAL_INTERFACE, "NotifyPointerButton", "oa{sv}iu", "", 
                              [this](sdbus::MethodCall call) { NotifyPointerButton(std::move(call)); });
        
        object->registerMethod(PORTAL_INTERFACE, "NotifyKeyboardKeycode", "oa{sv}iu", "", 
                              [this](sdbus::MethodCall call) { NotifyKeyboardKeycode(std::move(call)); });
        
        object->registerMethod(PORTAL_INTERFACE, "NotifyPointerAxis", "oa{sv}dd", "", 
                              [this](sdbus::MethodCall call) { NotifyPointerAxis(std::move(call)); });
        
        // Modern EIS (Emulated Input Server) method - this is what deskflow actually uses!
        object->registerMethod(PORTAL_INTERFACE, "ConnectToEIS", "osa{sv}", "h", 
                              [this](sdbus::MethodCall call) { ConnectToEIS(std::move(call)); });
        object->registerProperty(PORTAL_INTERFACE, "version", "u", [](sdbus::PropertyGetReply& reply) -> void { reply << (uint)2; });
        // Finalize the object
        object->finishRegistration();
        
        std::cout << "Portal D-Bus interface registered at " << PORTAL_NAME << std::endl;
        std::cout << "Portal registered on SESSION bus (not system bus)" << std::endl;
        return true;
        
    } catch (const sdbus::Error& e) {
        std::cerr << "Failed to initialize D-Bus portal: " << e.what() << std::endl;
        std::cerr << "This is normal if another portal is already running or if running outside a desktop session." << std::endl;
        cleanup();
        return false;
    }
}

void Portal::cleanup() {
    running = false;
    
    if (object) {
        object.reset();
    }
    
    if (connection) {
        connection.reset();
    }
}

void Portal::run() {
    if (!connection) return;
    
    running = true;
    
    std::cout << "🔄 Starting proper sdbus D-Bus event loop..." << std::endl;
    std::cout << "📡 Portal ready to receive D-Bus calls!" << std::endl;
    
    try {
        // Use the official sdbus event loop 
        // This will block and properly handle all incoming D-Bus method calls
        connection->enterEventLoop();
    } catch (const sdbus::Error& e) {
        std::cerr << "D-Bus error in portal loop: " << e.what() << std::endl;
    }
    
    std::cout << "🛑 D-Bus event loop stopped" << std::endl;
}

void Portal::stop() {
    running = false;
    // Exit the sdbus event loop cleanly
    if (connection) {
        connection->leaveEventLoop();
    }
}

void Portal::CreateSession(sdbus::MethodCall call) {
    std::cout << "🔥 RemoteDesktop CreateSession called!" << std::endl;
    std::cout << "📋 FLOW: Step 1/4 - CreateSession" << std::endl;
    std::cout << "🎯 This indicates deskflow found our portal!" << std::endl;
    
    // Extract parameters
    std::map<std::string, sdbus::Variant> options;
    call >> options;
    
    std::cout << "Options received:" << std::endl;
    for (const auto& option : options) {
        std::cout << "  " << option.first << std::endl;
    }
    
    // Create session response
    std::map<std::string, sdbus::Variant> response;
    response["session_handle"] = sdbus::Variant(std::string("/org/freedesktop/portal/desktop/session/1"));
    
    auto reply = call.createReply();
    reply << static_cast<uint32_t>(0); // Success
    reply << response;
    reply.send();
    
    std::cout << "✅ CreateSession completed successfully" << std::endl;
    std::cout << "📋 NEXT: Client should call SelectDevices or Start" << std::endl;
}

void Portal::SelectSources(sdbus::MethodCall call) {
    std::cout << "🔥 RemoteDesktop SelectSources called!" << std::endl;
    
    // Extract parameters
    sdbus::ObjectPath session_handle;
    std::map<std::string, sdbus::Variant> options;
    call >> session_handle >> options;
    
    std::cout << "Session handle: " << session_handle << std::endl;
    std::cout << "Options received:" << std::endl;
    for (const auto& option : options) {
        std::cout << "  " << option.first << std::endl;
    }
    
    // Response - allow all sources
    std::map<std::string, sdbus::Variant> response;
    response["types"] = sdbus::Variant(static_cast<uint32_t>(7)); // keyboard | pointer | touchscreen
    
    auto reply = call.createReply();
    reply << static_cast<uint32_t>(0); // Success
    reply << response;
    reply.send();
    
    std::cout << "✅ SelectSources completed for session: " << session_handle << std::endl;
}

void Portal::SelectDevices(sdbus::MethodCall call) {
    std::cout << "🔥 RemoteDesktop SelectDevices called!" << std::endl;
    std::cout << "📋 FLOW: Step 2/4 - SelectDevices" << std::endl;
    
    // Extract parameters
    sdbus::ObjectPath session_handle;
    std::map<std::string, sdbus::Variant> options;
    call >> session_handle >> options;
    
    std::cout << "Session handle: " << session_handle << std::endl;
    std::cout << "Options received:" << std::endl;
    for (const auto& option : options) {
        std::cout << "  " << option.first << std::endl;
    }
    
    // Response - allow all devices
    std::map<std::string, sdbus::Variant> response;
    response["types"] = sdbus::Variant(static_cast<uint32_t>(7)); // keyboard | pointer | touchscreen
    
    auto reply = call.createReply();
    reply << static_cast<uint32_t>(0); // Success
    reply << response;
    reply.send();
    
    std::cout << "✅ SelectDevices completed for session: " << session_handle << std::endl;
    std::cout << "📋 NEXT: Client should call Start" << std::endl;
}

void Portal::Start(sdbus::MethodCall call) {
    std::cout << "🔥 RemoteDesktop Start called - This is where the magic happens!" << std::endl;
    std::cout << "📋 FLOW: Step 3/4 - Start session" << std::endl;
    std::cout << "🎯 If you see this, deskflow is following the portal flow correctly!" << std::endl;

    // Extract parameters
    sdbus::ObjectPath session_handle;
    std::string parent_window;
    std::map<std::string, sdbus::Variant> options;
    call >> session_handle >> parent_window >> options;
    
    // Create a libei receiver context for this session
    struct ei* ei_context = ei_new_receiver(nullptr);

    if (!ei_context) {
        std::cerr << "Failed to create libei context for remote session" << std::endl;
        auto reply = call.createReply();
        reply << static_cast<uint32_t>(1); // Error
        reply << std::map<std::string, sdbus::Variant>{};
        reply.send();
        return;
    }
    
    ei_configure_name(ei_context, "Hyprland Remote Desktop Session");
    
    // Get the file descriptor that the remote client will use
    int ei_fd = ei_get_fd(ei_context);
    if (ei_fd < 0) {
        std::cerr << "Failed to get libei file descriptor" << std::endl;
        ei_unref(ei_context);
        auto reply = call.createReply();
        reply << static_cast<uint32_t>(1); // Error
        reply << std::map<std::string, sdbus::Variant>{};
        reply.send();
        return;
    }
    
    // Start the remote desktop session
    std::map<std::string, sdbus::Variant> response;
    response["devices"] = sdbus::Variant(static_cast<uint32_t>(7)); // keyboard | pointer | touchscreen
    
    // Pass the file descriptor to the client
    // Note: This would typically be done via D-Bus file descriptor passing
    // For now, we'll start the libei handler in a background thread
    if (libei_handler) {
        std::thread([this, ei_context]() {
            // Set up the libei context in the handler
            libei_handler->ei_context = ei_context;
            libei_handler->run();
        }).detach();
    }
    
    auto reply = call.createReply();
    reply << static_cast<uint32_t>(0); // Success
    reply << response;
    reply.send();
    
    std::cout << "Start completed - remote desktop session active for session: " << session_handle << std::endl;
    std::cout << "LibEI file descriptor: " << ei_fd << " (ready for remote input)" << std::endl;
    std::cout << "📋 NEXT: Client should now call NotifyPointer*/NotifyKeyboard* methods" << std::endl;
}

void Portal::NotifyPointerMotion(sdbus::MethodCall call) {
    std::cout << "🖱️ NotifyPointerMotion called!" << std::endl;
    std::cout << "📋 FLOW: Step 4/4 - Input events (Mouse Motion)" << std::endl;
    
    // Extract parameters
    sdbus::ObjectPath session_handle;
    std::map<std::string, sdbus::Variant> options;
    double dx, dy;
    call >> session_handle >> options >> dx >> dy;
    
    std::cout << "Session: " << session_handle << ", Motion: dx=" << dx << ", dy=" << dy << std::endl;
    
    // Get current time for wayland events
    uint32_t time = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    
    // Forward to virtual pointer via libei handler's virtual pointer
    if (libei_handler && libei_handler->pointer) {
        libei_handler->pointer->send_motion(time, dx, dy);
        libei_handler->pointer->send_frame();
        std::cout << "✅ Motion forwarded to virtual pointer" << std::endl;
    } else {
        std::cout << "❌ No virtual pointer available" << std::endl;
    }
    
    auto reply = call.createReply();
    reply.send();
}

void Portal::NotifyPointerButton(sdbus::MethodCall call) {
    std::cout << "🖱️ NotifyPointerButton called!" << std::endl;
    
    // Extract parameters
    sdbus::ObjectPath session_handle;
    std::map<std::string, sdbus::Variant> options;
    int32_t button;
    uint32_t state;
    call >> session_handle >> options >> button >> state;
    
    std::cout << "Session: " << session_handle << ", Button: " << button << ", State: " << state << std::endl;
    
    // Get current time for wayland events
    uint32_t time = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    
    // Forward to virtual pointer
    if (libei_handler && libei_handler->pointer) {
        libei_handler->pointer->send_button(time, static_cast<uint32_t>(button), state);
        libei_handler->pointer->send_frame();
        std::cout << "✅ Button event forwarded to virtual pointer" << std::endl;
    } else {
        std::cout << "❌ No virtual pointer available" << std::endl;
    }
    
    auto reply = call.createReply();
    reply.send();
}

void Portal::NotifyKeyboardKeycode(sdbus::MethodCall call) {
    std::cout << "⌨️ NotifyKeyboardKeycode called!" << std::endl;
    
    // Extract parameters
    sdbus::ObjectPath session_handle;
    std::map<std::string, sdbus::Variant> options;
    int32_t keycode;
    uint32_t state;
    call >> session_handle >> options >> keycode >> state;
    
    std::cout << "Session: " << session_handle << ", Keycode: " << keycode << ", State: " << state << std::endl;
    
    // Get current time for wayland events
    uint32_t time = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    
    // Forward to virtual keyboard
    if (libei_handler && libei_handler->keyboard) {
        libei_handler->keyboard->send_key(time, static_cast<uint32_t>(keycode), state);
        std::cout << "✅ Key event forwarded to virtual keyboard" << std::endl;
    } else {
        std::cout << "❌ No virtual keyboard available" << std::endl;
    }
    
    auto reply = call.createReply();
    reply.send();
}

void Portal::NotifyPointerAxis(sdbus::MethodCall call) {
    std::cout << "🖱️ NotifyPointerAxis called!" << std::endl;
    
    // Extract parameters
    sdbus::ObjectPath session_handle;
    std::map<std::string, sdbus::Variant> options;
    double dx, dy;
    call >> session_handle >> options >> dx >> dy;
    
    std::cout << "Session: " << session_handle << ", Axis: dx=" << dx << ", dy=" << dy << std::endl;
    
    // Get current time for wayland events
    uint32_t time = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    
    // Forward to virtual pointer
    if (libei_handler && libei_handler->pointer) {
        // Send scroll events for both axes if non-zero
        if (dx != 0.0) {
            libei_handler->pointer->send_axis(time, WL_POINTER_AXIS_HORIZONTAL_SCROLL, dx);
        }
        if (dy != 0.0) {
            libei_handler->pointer->send_axis(time, WL_POINTER_AXIS_VERTICAL_SCROLL, dy);
        }
        libei_handler->pointer->send_frame();
        std::cout << "✅ Axis event forwarded to virtual pointer" << std::endl;
    } else {
        std::cout << "❌ No virtual pointer available" << std::endl;
    }
    
    auto reply = call.createReply();
    reply.send();
}

void Portal::ConnectToEIS(sdbus::MethodCall call) {
    std::cout << "🔥 RemoteDesktop ConnectToEIS called!" << std::endl;
    std::cout << "📋 FLOW: Step 5/5 - Connect to EIS (Modern approach!)" << std::endl;
    std::cout << "🎯 THIS IS THE KEY METHOD! Deskflow uses this for input!" << std::endl;
    
    // Extract parameters
    sdbus::ObjectPath session_handle;
    std::map<std::string, sdbus::Variant> options;
    call >> session_handle >> options;
    
    std::cout << "Session handle: " << session_handle << std::endl;
    std::cout << "Options received:" << std::endl;
    for (const auto& option : options) {
        std::cout << "  " << option.first << std::endl;
    }
    
    // Create a new EIS receiver context for this client connection
    struct ei* ei_context = ei_new_receiver(nullptr);
    if (!ei_context) {
        std::cerr << "Failed to create EIS receiver context" << std::endl;
        call.createErrorReply(sdbus::Error("org.freedesktop.portal.Error.Failed", "Failed to create EIS context")).send();
        return;
    }
    
    ei_configure_name(ei_context, "Hyprland Remote Desktop EIS");
    
    // Get the file descriptor that the client will use to connect
    int ei_fd = ei_get_fd(ei_context);
    if (ei_fd < 0) {
        std::cerr << "Failed to get EIS file descriptor" << std::endl;
        ei_unref(ei_context);
        call.createErrorReply(sdbus::Error("org.freedesktop.portal.Error.Failed", "Failed to get EIS file descriptor")).send();
        return;
    }
    
    std::cout << "✅ Created EIS context with fd: " << ei_fd << std::endl;
    
    // Start the EIS event handler in a background thread
    if (libei_handler) {
        std::thread([this, ei_context]() {
            // Set up the libei context in the handler
            libei_handler->ei_context = ei_context;
            libei_handler->run();
        }).detach();
        
        std::cout << "✅ Started EIS handler thread" << std::endl;
    }
    
    // Return the file descriptor to the client via D-Bus file descriptor passing
    auto reply = call.createReply();
    
    // Create a unix file descriptor object and pass it to the client
    sdbus::UnixFd unix_fd{ei_fd};
    reply << unix_fd;
    reply.send();
    
    std::cout << "✅ ConnectToEIS completed - file descriptor sent to client" << std::endl;
    std::cout << "📋 Client can now use libei to send input events through fd " << ei_fd << std::endl;
} 