#include "portal.h"
#include "libei_handler.h"
#include "wayland_virtual_keyboard.h"
#include "wayland_virtual_pointer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <algorithm>
#include <sys/mman.h>
#include <fcntl.h>

extern "C" {
#include <libei.h>
#include "libei-1.0/libeis.h"
#include <wayland-client-protocol.h>
}

static const char* PORTAL_INTERFACE = "org.freedesktop.impl.portal.RemoteDesktop";
static const char* PORTAL_PATH = "/org/freedesktop/portal/desktop";

// Use development name if requested, otherwise use standard name
static const char* PORTAL_NAME = "org.freedesktop.impl.portal.desktop.hypr-remote";

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

        object->registerMethod(PORTAL_INTERFACE, "NotifyKeyboardKeysym", "oa{sv}iu", "",
                              [this](sdbus::MethodCall call) { NotifyKeyboardKeysym(std::move(call)); });

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
    
    // Extract parameters according to D-Bus signature "oosa{sv}"
    sdbus::ObjectPath request_handle;
    sdbus::ObjectPath session_handle;
    std::string app_id;
    std::map<std::string, sdbus::Variant> options;
    
    try {
        call >> request_handle >> session_handle >> app_id >> options;
        
        std::cout << "Request handle: " << request_handle << std::endl;
        std::cout << "Session handle: " << session_handle << std::endl;
        std::cout << "App ID: " << app_id << std::endl;
        std::cout << "Options received:" << std::endl;
        for (const auto& option : options) {
            std::cout << "  " << option.first << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error extracting CreateSession parameters: " << e.what() << std::endl;
        auto reply = call.createReply();
        reply << static_cast<uint32_t>(1); // Error
        reply << std::map<std::string, sdbus::Variant>{};
        reply.send();
        return;
    }
    
    // Create session response
    std::map<std::string, sdbus::Variant> response;
    response["session_handle"] = sdbus::Variant(session_handle);
    
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
    
    // Extract parameters according to D-Bus signature "oosa{sv}"
    sdbus::ObjectPath request_handle;
    sdbus::ObjectPath session_handle;
    std::string app_id;
    std::map<std::string, sdbus::Variant> options;
    
    try {
        call >> request_handle >> session_handle >> app_id >> options;
        
        std::cout << "Request handle: " << request_handle << std::endl;
        std::cout << "Session handle: " << session_handle << std::endl;
        std::cout << "App ID: " << app_id << std::endl;
        std::cout << "Options received:" << std::endl;
        for (const auto& option : options) {
            std::cout << "  " << option.first << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error extracting SelectDevices parameters: " << e.what() << std::endl;
        auto reply = call.createReply();
        reply << static_cast<uint32_t>(1); // Error
        reply << std::map<std::string, sdbus::Variant>{};
        reply.send();
        return;
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

    // Extract parameters according to D-Bus signature "oossa{sv}"
    sdbus::ObjectPath request_handle;
    sdbus::ObjectPath session_handle;
    std::string app_id;
    std::string parent_window;
    std::map<std::string, sdbus::Variant> options;
    
    try {
        call >> request_handle >> session_handle >> app_id >> parent_window >> options;
        
        std::cout << "Request handle: " << request_handle << std::endl;
        std::cout << "Session handle: " << session_handle << std::endl;
        std::cout << "App ID: " << app_id << std::endl;
        std::cout << "Parent window: " << parent_window << std::endl;
        std::cout << "Options received:" << std::endl;
        for (const auto& option : options) {
            std::cout << "  " << option.first << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error extracting Start parameters: " << e.what() << std::endl;
        auto reply = call.createReply();
        reply << static_cast<uint32_t>(1); // Error
        reply << std::map<std::string, sdbus::Variant>{};
        reply.send();
        return;
    }
    
    // Check if we have a working LibEI handler
    if (!libei_handler) {
        std::cerr << "No LibEI handler available for remote session" << std::endl;
        auto reply = call.createReply();
        reply << static_cast<uint32_t>(1); // Error
        reply << std::map<std::string, sdbus::Variant>{};
        reply.send();
        return;
    }
    
    std::cout << "✅ Using existing LibEI handler for input processing" << std::endl;
    
    // Start the remote desktop session
    std::map<std::string, sdbus::Variant> response;
    response["devices"] = sdbus::Variant(static_cast<uint32_t>(7)); // keyboard | pointer | touchscreen
    
    auto reply = call.createReply();
    reply << static_cast<uint32_t>(0); // Success
    reply << response;
    reply.send();
    
    std::cout << "Start completed - remote desktop session active for session: " << session_handle << std::endl;
    std::cout << "LibEI handler ready for input processing" << std::endl;
    std::cout << "📋 NEXT: Client should now call ConnectToEIS for modern input" << std::endl;
}

void Portal::NotifyPointerMotion(sdbus::MethodCall call) {
    std::cout << "🖱️ NotifyPointerMotion called!" << std::endl;
    std::cout << "📋 FLOW: Step 4/4 - Input events (Mouse Motion)" << std::endl;
    std::cout << "🎯 DESKFLOW IS USING LEGACY NOTIFY METHODS!" << std::endl;
    
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

void Portal::NotifyKeyboardKeysym(sdbus::MethodCall call) {
    std::cout << "⌨️ NotifyKeyboardKeysym called!" << std::endl;

    // Extract parameters
    sdbus::ObjectPath session_handle;
    std::map<std::string, sdbus::Variant> options;
    int32_t keysym;
    uint32_t state;
    call >> session_handle >> options >> keysym >> state;

    std::cout << "Session: " << session_handle << ", Keysym: " << keysym << ", State: " << state << std::endl;

    // Get current time for wayland events
    uint32_t time = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());

    // Forward to virtual keyboard
    if (libei_handler && libei_handler->keyboard) {
        libei_handler->keyboard->send_keysym(time, static_cast<uint32_t>(keysym), state);
        std::cout << "✅ Keysym event forwarded to virtual keyboard" << std::endl;
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
    
    // Forward to virtual pointer with proper Wayland scroll protocol
    if (libei_handler && libei_handler->pointer) {
        // Set axis source for legacy scroll events
        libei_handler->pointer->send_axis_source(WL_POINTER_AXIS_SOURCE_WHEEL);
        
        // Send scroll events for both axes if non-zero
        if (dx != 0.0) {
            libei_handler->pointer->send_axis(time, WL_POINTER_AXIS_HORIZONTAL_SCROLL, dx, dy);
            libei_handler->pointer->send_axis_stop(time, WL_POINTER_AXIS_HORIZONTAL_SCROLL);
        }
        if (dy != 0.0) {
            libei_handler->pointer->send_axis(time, WL_POINTER_AXIS_VERTICAL_SCROLL, dx, dy);
            libei_handler->pointer->send_axis_stop(time, WL_POINTER_AXIS_VERTICAL_SCROLL);
        }
        libei_handler->pointer->send_frame();
        std::cout << "✅ Legacy axis event forwarded with proper scroll protocol" << std::endl;
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
    
    // Extract parameters according to D-Bus signature "osa{sv}"
    sdbus::ObjectPath session_handle;
    std::string app_id;
    std::map<std::string, sdbus::Variant> options;
    
    try {
        call >> session_handle >> app_id >> options;
        
        std::cout << "Session handle: " << session_handle << std::endl;
        std::cout << "App ID: " << app_id << std::endl;
        std::cout << "Options received:" << std::endl;
        for (const auto& option : options) {
            std::cout << "  " << option.first << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error extracting ConnectToEIS parameters: " << e.what() << std::endl;
        call.createErrorReply(sdbus::Error("org.freedesktop.portal.Error.Failed", "Failed to extract parameters")).send();
        return;
    }
    
    if (!libei_handler || !libei_handler->keyboard || !libei_handler->pointer) {
        std::cerr << "Virtual devices not available" << std::endl;
        call.createErrorReply(sdbus::Error("org.freedesktop.portal.Error.Failed", "Virtual devices not available")).send();
        return;
    }
    
    // Create a socket pair - one end for deskflow, one end for our EIS server
    int socket_pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pair) == -1) {
        std::cerr << "Error creating socket pair: " << strerror(errno) << std::endl;
        call.createErrorReply(sdbus::Error("org.freedesktop.portal.Error.Failed", "Failed to create socket pair")).send();
        return;
    }
    
    int client_fd = socket_pair[0];  // This goes to deskflow
    int server_fd = socket_pair[1];  // This stays with us for EIS server
    
    std::cout << "✅ Created socket pair - client_fd: " << client_fd << ", server_fd: " << server_fd << std::endl;
    
    // Start a thread to run a proper EIS server
    std::thread([this, server_fd]() {
        std::cout << "📡 Starting proper EIS server thread..." << std::endl;
        
        // Create EIS server context (similar to hyprland-eis)
        struct eis* eis_context = eis_new(nullptr);
        if (!eis_context) {
            std::cerr << "Failed to create EIS server context" << std::endl;
            close(server_fd);
            return;
        }
        
        // Set up logging (optional, for debugging)
        // eis_log_set_priority(eis_context, EIS_LOG_PRIORITY_DEBUG);
        
        std::cout << "✅ EIS server context created" << std::endl;
        
        // Create a temporary socket and immediately connect our FD to it
        // This is a workaround since libeis may not support direct FD setup
        
        // Alternative approach: Create a proper EIS server socket and handle the connection
        char socket_path[256];
        snprintf(socket_path, sizeof(socket_path), "/tmp/hypr-portal-eis-%d", getpid());
        
        // Setup EIS backend with temporary socket
        int rc = eis_setup_backend_socket(eis_context, socket_path);
        if (rc != 0) {
            std::cerr << "Failed to setup EIS backend socket: " << strerror(errno) << std::endl;
            eis_unref(eis_context);
            close(server_fd);
            return;
        }
        
        std::cout << "✅ EIS backend socket created at: " << socket_path << std::endl;
        
        // Now we need to bridge between our socket_pair and the EIS socket
        // Start a bridge thread to forward data between them
        std::thread bridge_thread([server_fd, socket_path]() {
            std::cout << "🌉 Starting socket bridge..." << std::endl;
            
            // Connect to the EIS socket
            int eis_sock = socket(AF_UNIX, SOCK_STREAM, 0);
            if (eis_sock == -1) {
                std::cerr << "Failed to create bridge socket" << std::endl;
                return;
            }
            
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
            
            // Wait a bit for the EIS server to start
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            if (connect(eis_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
                std::cerr << "Failed to connect to EIS socket: " << strerror(errno) << std::endl;
                close(eis_sock);
                return;
            }
            
            std::cout << "✅ Bridge connected to EIS socket" << std::endl;
            
            // Bridge data bidirectionally between server_fd and eis_sock
            fd_set read_fds;
            char buffer[4096];
            
            // Make sockets non-blocking for better performance
            int flags = fcntl(server_fd, F_GETFL, 0);
            fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
            flags = fcntl(eis_sock, F_GETFL, 0);
            fcntl(eis_sock, F_SETFL, flags | O_NONBLOCK);
            
            while (true) {
                FD_ZERO(&read_fds);
                FD_SET(server_fd, &read_fds);
                FD_SET(eis_sock, &read_fds);
                
                int max_fd = std::max(server_fd, eis_sock);
                struct timeval timeout = {0, 100000}; // 100ms timeout for better responsiveness
                
                int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);
                
                if (activity < 0) {
                    std::cerr << "Bridge select error: " << strerror(errno) << std::endl;
                    break;
                }
                
                if (activity == 0) continue; // timeout
                
                // Forward data from deskflow (server_fd) to EIS server (eis_sock)
                if (FD_ISSET(server_fd, &read_fds)) {
                    ssize_t bytes = read(server_fd, buffer, sizeof(buffer));
                    if (bytes <= 0) {
                        if (bytes == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                            std::cout << "Deskflow disconnected from bridge" << std::endl;
                            break;
                        }
                    } else {
                        if (write(eis_sock, buffer, bytes) != bytes) {
                            std::cerr << "Failed to forward data to EIS server" << std::endl;
                            break;
                        }
                        //std::cout << "🔄 Forwarded " << bytes << " bytes from deskflow to EIS" << std::endl;
                    }
                }
                
                // Forward data from EIS server (eis_sock) to deskflow (server_fd)
                if (FD_ISSET(eis_sock, &read_fds)) {
                    ssize_t bytes = read(eis_sock, buffer, sizeof(buffer));
                    if (bytes <= 0) {
                        if (bytes == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                            std::cout << "EIS server disconnected from bridge" << std::endl;
                            break;
                        }
                    } else {
                        if (write(server_fd, buffer, bytes) != bytes) {
                            std::cerr << "Failed to forward data to deskflow" << std::endl;
                            break;
                        }
                        //std::cout << "🔄 Forwarded " << bytes << " bytes from EIS to deskflow" << std::endl;
                    }
                }
            }
            
            std::cout << "🌉 Socket bridge stopped" << std::endl;
            close(eis_sock);
        });
        bridge_thread.detach();
        
        // Run the EIS server event loop (adapted from hyprland-eis)
        std::cout << "🚀 Starting EIS server event loop..." << std::endl;
        
        bool stop = false;
        struct pollfd fds = {
            .fd = eis_get_fd(eis_context),
            .events = POLLIN,
            .revents = 0,
        };
        
        while (!stop) {
            int nevents = poll(&fds, 1, 100); // Reduced timeout to 100ms for better responsiveness
            if (nevents == -1) {
                std::cerr << "EIS poll error: " << strerror(errno) << std::endl;
                break;
            }
            
            if (nevents == 0) continue; // timeout
            
            // Process all pending EIS events in one go - this is crucial for scroll
            eis_dispatch(eis_context);
            
            // Handle ALL events in the queue immediately
            struct eis_event* event;
            int event_count = 0;
            while ((event = eis_get_event(eis_context)) != nullptr) {
                event_count++;
                //std::cout << "🎯 EIS: Processing event " << event_count << " type=" << eis_event_get_type(event) << std::endl;
                
                // Handle EIS events and forward to virtual devices
                handle_eis_event(event);
                eis_event_unref(event);
            }
            
            if (event_count > 0) {
                std::cout << "📊 EIS: Processed " << event_count << " events in this cycle" << std::endl;
            }
        }
        
        std::cout << "📡 EIS server thread stopped" << std::endl;
        unlink(socket_path); // Clean up socket file
        eis_unref(eis_context);
        close(server_fd);
    }).detach();
    
    // Return the client file descriptor to deskflow
    auto reply = call.createReply();
    
    // Create a unix file descriptor object and pass it to the client
    sdbus::UnixFd unix_fd{client_fd};
    reply << unix_fd;
    reply.send();
    
    std::cout << "✅ ConnectToEIS completed - socket fd sent to deskflow" << std::endl;
    std::cout << "🎯 Deskflow can now send EIS events through fd " << client_fd << std::endl;
    std::cout << "📡 Proper EIS server thread is running with socket bridge" << std::endl;
}

void Portal::handle_eis_event(struct eis_event* event) {
    enum eis_event_type type = eis_event_get_type(event);
    
    // Log all events for debugging
    const char* event_name = "UNKNOWN";
    switch (type) {
        case EIS_EVENT_CLIENT_CONNECT: event_name = "CLIENT_CONNECT"; break;
        case EIS_EVENT_CLIENT_DISCONNECT: event_name = "CLIENT_DISCONNECT"; break;
        case EIS_EVENT_SEAT_BIND: event_name = "SEAT_BIND"; break;
        case EIS_EVENT_DEVICE_START_EMULATING: event_name = "DEVICE_START_EMULATING"; break;
        case EIS_EVENT_DEVICE_STOP_EMULATING: event_name = "DEVICE_STOP_EMULATING"; break;
        case EIS_EVENT_POINTER_MOTION: event_name = "POINTER_MOTION"; break;
        case EIS_EVENT_POINTER_MOTION_ABSOLUTE: event_name = "POINTER_MOTION_ABSOLUTE"; break;
        case EIS_EVENT_BUTTON_BUTTON: event_name = "BUTTON_BUTTON"; break;
        case EIS_EVENT_SCROLL_DELTA: event_name = "SCROLL_DELTA"; break;
        case EIS_EVENT_SCROLL_DISCRETE: event_name = "SCROLL_DISCRETE"; break;
        case EIS_EVENT_KEYBOARD_KEY: event_name = "KEYBOARD_KEY"; break;
        case EIS_EVENT_FRAME: event_name = "FRAME"; break;
        default: event_name = "UNKNOWN"; break;
    }
    //std::cout << "🔥 EIS EVENT: " << event_name << " (type=" << type << ")" << std::endl;
    
    switch (type) {
        case EIS_EVENT_CLIENT_CONNECT: {
            struct eis_client* client = eis_event_get_client(event);
            std::cout << "🔌 EIS: Client connected: " << eis_client_get_name(client) << std::endl;
            
            // Accept the client connection
            eis_client_connect(client);
            
            // Add a seat for this client (required for devices)
            struct eis_seat* seat = eis_client_new_seat(client, "hyprland-portal-seat");
            eis_seat_configure_capability(seat, EIS_DEVICE_CAP_POINTER);
            eis_seat_configure_capability(seat, EIS_DEVICE_CAP_POINTER_ABSOLUTE);
            eis_seat_configure_capability(seat, EIS_DEVICE_CAP_KEYBOARD);
            eis_seat_configure_capability(seat, EIS_DEVICE_CAP_BUTTON);
            eis_seat_configure_capability(seat, EIS_DEVICE_CAP_SCROLL);
            eis_seat_add(seat);
            
            std::cout << "💺 EIS: Seat added for client with capabilities" << std::endl;
            break;
        }
        
        case EIS_EVENT_CLIENT_DISCONNECT:
            std::cout << "🔌 EIS: Client disconnected" << std::endl;
            break;
            
        case EIS_EVENT_SEAT_BIND: {
            struct eis_seat* seat = eis_event_get_seat(event);
            std::cout << "💺 EIS: Seat bound by client" << std::endl;
            
            // Add pointer device
            struct eis_device* pointer = eis_seat_new_device(seat);
            eis_device_configure_name(pointer, "Hyprland Portal Pointer");
            eis_device_configure_capability(pointer, EIS_DEVICE_CAP_POINTER);
            eis_device_configure_capability(pointer, EIS_DEVICE_CAP_POINTER_ABSOLUTE);
            eis_device_configure_capability(pointer, EIS_DEVICE_CAP_BUTTON);
            eis_device_configure_capability(pointer, EIS_DEVICE_CAP_SCROLL);
            
            // Set pointer region (screen size)
            struct eis_region* region = eis_device_new_region(pointer);
            eis_region_set_size(region, 1920, 1080); // TODO: Get actual screen size
            eis_region_add(region);
            
            eis_device_add(pointer);
            eis_device_resume(pointer);
            
            // Add keyboard device with proper keymap setup
            struct eis_device* keyboard = eis_seat_new_device(seat);
            eis_device_configure_name(keyboard, "Hyprland Portal Keyboard");
            eis_device_configure_capability(keyboard, EIS_DEVICE_CAP_KEYBOARD);
            
            // Set up a basic keymap for proper modifier key handling
            // This is crucial for key combinations like Meta+Enter to work
            const char* keymap_str = 
                "xkb_keymap {\n"
                "xkb_keycodes  { include \"evdev+aliases(qwerty)\" };\n"
                "xkb_types     { include \"complete\" };\n"
                "xkb_compat    { include \"complete\" };\n"
                "xkb_symbols   { include \"pc+us+inet(evdev)\" };\n"
                "xkb_geometry  { include \"pc(pc105)\" };\n"
                "};\n";
                
            // Create a memory file for the keymap
            size_t keymap_size = strlen(keymap_str);
            int memfd = memfd_create("keymap", MFD_CLOEXEC);
            if (memfd >= 0) {
                if (write(memfd, keymap_str, keymap_size) == (ssize_t)keymap_size) {
                    struct eis_keymap* keymap = eis_device_new_keymap(keyboard, 
                        EIS_KEYMAP_TYPE_XKB, memfd, keymap_size);
                    if (keymap) {
                        eis_keymap_add(keymap);
                        std::cout << "🗝️ EIS: Keymap configured for proper modifier handling" << std::endl;
                    }
                }
                close(memfd);
            }
            
            eis_device_add(keyboard);
            eis_device_resume(keyboard);
            
            std::cout << "🖱️ EIS: Pointer and keyboard devices added with enhanced features" << std::endl;
            break;
        }
        
        case EIS_EVENT_DEVICE_START_EMULATING: {
            struct eis_device* device = eis_event_get_device(event);
            std::cout << "🎮 EIS: Device started emulating: " << eis_device_get_name(device) << std::endl;
            break;
        }
        
        case EIS_EVENT_DEVICE_STOP_EMULATING: {
            struct eis_device* device = eis_event_get_device(event);
            std::cout << "🎮 EIS: Device stopped emulating: " << eis_device_get_name(device) << std::endl;
            break;
        }
        
        case EIS_EVENT_POINTER_MOTION: {
            double dx = eis_event_pointer_get_dx(event);
            double dy = eis_event_pointer_get_dy(event);
            
            std::cout << "🖱️ EIS: Pointer motion dx=" << dx << " dy=" << dy << std::endl;
            
            // Forward to virtual pointer
            if (libei_handler && libei_handler->pointer) {
                uint32_t time = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                libei_handler->pointer->send_motion(time, dx, dy);
                libei_handler->pointer->send_frame();
                std::cout << "✅ Motion forwarded to virtual pointer" << std::endl;
            }
            break;
        }
        
        case EIS_EVENT_POINTER_MOTION_ABSOLUTE: {
            double x = eis_event_pointer_get_absolute_x(event);
            double y = eis_event_pointer_get_absolute_y(event);
            
            std::cout << "🖱️ EIS: Pointer absolute motion x=" << x << " y=" << y << std::endl;
            
            // Forward to virtual pointer  
            if (libei_handler && libei_handler->pointer) {
                uint32_t time = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                libei_handler->pointer->send_motion_absolute(time, 
                    static_cast<uint32_t>(x), static_cast<uint32_t>(y), 1920, 1080);
                libei_handler->pointer->send_frame();
                std::cout << "✅ Absolute motion forwarded to virtual pointer" << std::endl;
            }
            break;
        }
        
        case EIS_EVENT_BUTTON_BUTTON: {
            uint32_t button = eis_event_button_get_button(event);
            bool is_press = eis_event_button_get_is_press(event);
            
            std::cout << "🖱️ EIS: Button " << (is_press ? "press" : "release") << " button=" << button << std::endl;
            
            // Forward to virtual pointer
            if (libei_handler && libei_handler->pointer) {
                uint32_t time = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                libei_handler->pointer->send_button(time, button, is_press ? 1 : 0);
                libei_handler->pointer->send_frame();
                std::cout << "✅ Button event forwarded to virtual pointer" << std::endl;
            }
            break;
        }
        
        case EIS_EVENT_SCROLL_DELTA: {
            double dx = eis_event_scroll_get_dx(event);
            double dy = eis_event_scroll_get_dy(event);
            
            std::cout << "🖱️ EIS: Scroll delta dx=" << dx << " dy=" << dy << std::endl;
            
            // Debug: Check if we have the required components
            std::cout << "🔍 DEBUG: libei_handler=" << (libei_handler ? "YES" : "NO") 
                      << ", pointer=" << (libei_handler && libei_handler->pointer ? "YES" : "NO") << std::endl;
            
            // Forward to virtual pointer with proper Wayland scroll protocol
            if (libei_handler && libei_handler->pointer) {
                uint32_t time = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                
                std::cout << "🎯 Sending scroll events with time=" << time << std::endl;
                
                // Set axis source - wheel is the most common source for EIS scroll events
                libei_handler->pointer->send_axis_source(WL_POINTER_AXIS_SOURCE_WHEEL);
                    
                // Scale the scroll values appropriately for Wayland
                double scale_factor = 15.0; // Good default for smooth scrolling
                
                if (dx != 0.0) {
                    std::cout << "🔄 Sending horizontal scroll: " << (dx * scale_factor) << std::endl;
                    libei_handler->pointer->send_axis(time, WL_POINTER_AXIS_HORIZONTAL_SCROLL, dx * scale_factor, dy);
                    // Send axis stop to complete the scroll event
                    libei_handler->pointer->send_axis_stop(time, WL_POINTER_AXIS_HORIZONTAL_SCROLL);
                }
                if (dy != 0.0) {
                    std::cout << "🔄 Sending vertical scroll: " << (dy * scale_factor) << std::endl;
                    libei_handler->pointer->send_axis(time, WL_POINTER_AXIS_VERTICAL_SCROLL, dx * scale_factor, dy);
                    // Send axis stop to complete the scroll event  
                    libei_handler->pointer->send_axis_stop(time, WL_POINTER_AXIS_VERTICAL_SCROLL);
                }
                libei_handler->pointer->send_frame();
                std::cout << "✅ Scroll delta forwarded with proper axis protocol" << std::endl;
            } else {
                std::cout << "❌ Cannot forward scroll - missing virtual pointer!" << std::endl;
            }
            break;
        }
        
        case EIS_EVENT_SCROLL_DISCRETE: {
            int32_t dx = eis_event_scroll_get_discrete_dx(event);
            int32_t dy = eis_event_scroll_get_discrete_dy(event);
            
            if (dx == 0 && dy == 0) {
                break;
                // Assume this is a vertical scroll event and give it a default value
                //dy = -1; // Negative = scroll up (standard)
                //std::cout << "🔄 Discrete values are 0, assuming vertical scroll step: dy=" << dy << std::endl;
            }

            std::cout << "🖱️ EIS: Scroll discrete dx=" << dx << " dy=" << dy << std::endl;
            
            // If discrete values are 0, assume vertical scroll with 1 step (common case)
                        
            // Forward discrete scroll if we have actual values (now that we fixed 0,0 case)
            if (libei_handler && libei_handler->pointer && (dx != 0 || dy != 0)) {
                uint32_t time = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                    
                // Set axis source for discrete scroll (wheel clicks)
                libei_handler->pointer->send_axis_source(WL_POINTER_AXIS_SOURCE_WHEEL);
                
                // Use standard scroll value per click - simple and effective
                double scroll_value = 15.0;
                int axis = dx == 0 ? WL_POINTER_AXIS_VERTICAL_SCROLL : WL_POINTER_AXIS_HORIZONTAL_SCROLL;
                libei_handler->pointer->send_axis_discrete(time, dx, dy);
                // libei_handler->pointer->send_axis_stop(time, axis);
            
                libei_handler->pointer->send_frame();
                std::cout << "✅ Scroll discrete forwarded (steps=" << dx << "," << dy << ")" << std::endl;
            } else {
                std::cout << "❌ No scroll to forward (dx=" << dx << " dy=" << dy << ") or no pointer available" << std::endl;
            }
            break;
        }
        
        case EIS_EVENT_KEYBOARD_KEY: {
            uint32_t keycode = eis_event_keyboard_get_key(event);
            bool is_press = eis_event_keyboard_get_key_is_press(event);
            
            std::cout << "⌨️ EIS: Keyboard " << (is_press ? "press" : "release") << " keycode=" << keycode << std::endl;
            
            // Debug: Check if we have the required components
            std::cout << "🔍 DEBUG: libei_handler=" << (libei_handler ? "YES" : "NO") 
                      << ", keyboard=" << (libei_handler && libei_handler->keyboard ? "YES" : "NO") << std::endl;
            
            // Forward to virtual keyboard with immediate modifier updates
            if (libei_handler && libei_handler->keyboard) {
                uint32_t time = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                    
                std::cout << "🎯 Processing key event with time=" << time << std::endl;
                
                // Update modifier state BEFORE sending the key event (using raw keycode)
                update_modifier_state(keycode, is_press);
                
                std::cout << "🔧 Current modifier state: depressed=" << modifier_state_depressed 
                         << ", latched=" << modifier_state_latched << ", locked=" << modifier_state_locked << std::endl;
                
                // Send modifier state first - this is crucial for key combinations like Meta+Enter
                libei_handler->keyboard->send_modifiers(modifier_state_depressed, 
                                                      modifier_state_latched,
                                                      modifier_state_locked, 
                                                      modifier_state_group);
                
                // Send the actual key event with the raw keycode (no conversion needed!)
                libei_handler->keyboard->send_key(time, keycode, is_press ? 1 : 0);
                
                // Send modifiers again after the key event to ensure state consistency
                libei_handler->keyboard->send_modifiers(modifier_state_depressed, 
                                                      modifier_state_latched,
                                                      modifier_state_locked, 
                                                      modifier_state_group);
                                                      
                std::cout << "✅ Key " << keycode << " (" << (is_press ? "pressed" : "released") 
                         << ") forwarded with modifier state: " << modifier_state_depressed << std::endl;
            } else {
                std::cout << "❌ Cannot forward key - missing virtual keyboard!" << std::endl;
            }
            break;
        }
        
        case EIS_EVENT_FRAME:
            // Frame events group related events together - just log for now
            std::cout << "📸 EIS: Frame event" << std::endl;
            break;
            
        default:
            std::cout << "❓ EIS: Unhandled event type: " << type << std::endl;
            break;
    }
}

void Portal::update_modifier_state(uint32_t keycode, bool is_press) {
    // EIS uses raw Linux input keycodes (NOT XKB keycodes with +8 offset)
    // These are the standard Linux input event keycodes
    bool is_modifier = false;
    uint32_t modifier_mask = 0;
    
    switch (keycode) {
        case 42:  // Shift_L (raw keycode 42)
        case 54:  // Shift_R (raw keycode 54)
            is_modifier = true;
            modifier_mask = MOD_SHIFT;
            std::cout << "🔧 Detected SHIFT key: " << keycode << std::endl;
            break;
            
        case 29:  // Control_L (raw keycode 29)
        case 97:  // Control_R (raw keycode 97)
            is_modifier = true;
            modifier_mask = MOD_CTRL;
            std::cout << "🔧 Detected CTRL key: " << keycode << std::endl;
            break;
            
        case 56:  // Alt_L (raw keycode 56)
        case 100: // Alt_R (raw keycode 100)
            is_modifier = true;
            modifier_mask = MOD_ALT;
            std::cout << "🔧 Detected ALT key: " << keycode << std::endl;
            break;
            
        case 125: // Super_L (raw keycode 125) - Meta/Windows key
        case 126: // Super_R (raw keycode 126)
            is_modifier = true;
            modifier_mask = MOD_META;
            std::cout << "🔧 Detected META/SUPER key: " << keycode << std::endl;
            break;
            
        case 58:  // Caps_Lock (raw keycode 58)
            // Caps lock is special - toggle on press only
            if (is_press) {
                modifier_state_locked ^= MOD_CAPS; // Toggle caps lock state
                std::cout << "🔒 Caps Lock toggled: " << (modifier_state_locked & MOD_CAPS ? "ON" : "OFF") << std::endl;
            }
            return;
            
        case 69:  // Num_Lock (raw keycode 69)
            // Num lock is special - toggle on press only
            if (is_press) {
                modifier_state_locked ^= MOD_NUM; // Toggle num lock state
                std::cout << "🔢 Num Lock toggled: " << (modifier_state_locked & MOD_NUM ? "ON" : "OFF") << std::endl;
            }
            return;
    }
    
    if (is_modifier) {
        if (is_press) {
            modifier_state_depressed |= modifier_mask;
            std::cout << "🔧 Modifier pressed: " << modifier_mask << " (state: " << modifier_state_depressed << ")" << std::endl;
        } else {
            modifier_state_depressed &= ~modifier_mask;
            std::cout << "🔧 Modifier released: " << modifier_mask << " (state: " << modifier_state_depressed << ")" << std::endl;
        }
    } else {
        std::cout << "🔍 Non-modifier key: " << keycode << std::endl;
    }
} 