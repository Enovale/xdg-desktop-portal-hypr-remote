#include "wayland_virtual_keyboard.h"
#include <iostream>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory>
#include <optional>
#include <xkbcommon/xkbcommon.h>
#include <linux/input-event-codes.h>

// Taken almost wholesale from https://github.com/KDE/xdg-desktop-portal-kde/blob/master/src/waylandintegration.cpp#L450
namespace
{
struct XKBStateDeleter {
    void operator()(struct xkb_state *state) const
    {
        return xkb_state_unref(state);
    }
};
struct XKBKeymapDeleter {
    void operator()(struct xkb_keymap *keymap) const
    {
        return xkb_keymap_unref(keymap);
    }
};
struct XKBContextDeleter {
    void operator()(struct xkb_context *context) const
    {
        return xkb_context_unref(context);
    }
};
using ScopedXKBState = std::unique_ptr<struct xkb_state, XKBStateDeleter>;
using ScopedXKBKeymap = std::unique_ptr<struct xkb_keymap, XKBKeymapDeleter>;
using ScopedXKBContext = std::unique_ptr<struct xkb_context, XKBContextDeleter>;
}
class Xkb
{
public:
    struct Code {
        const uint32_t level;
        const uint32_t code;
    };
    std::optional<Code> keycodeFromKeysym(xkb_keysym_t keysym)
    {
        /* The offset between KEY_* numbering, and keycodes in the XKB evdev
         * dataset. */
        static const uint EVDEV_OFFSET = 8;

        auto layout = xkb_state_serialize_layout(m_state.get(), XKB_STATE_LAYOUT_EFFECTIVE);
        const xkb_keycode_t max = xkb_keymap_max_keycode(m_keymap.get());
        for (xkb_keycode_t keycode = xkb_keymap_min_keycode(m_keymap.get()); keycode < max; keycode++) {
            uint levelCount = xkb_keymap_num_levels_for_key(m_keymap.get(), keycode, layout);
            for (uint currentLevel = 0; currentLevel < levelCount; currentLevel++) {
                const xkb_keysym_t *syms;
                uint num_syms = xkb_keymap_key_get_syms_by_level(m_keymap.get(), keycode, layout, currentLevel, &syms);
                for (uint sym = 0; sym < num_syms; sym++) {
                    if (syms[sym] == keysym) {
                        return Code{currentLevel, keycode - EVDEV_OFFSET};
                    }
                }
            }
        }
        return {};
    }

    void keyboard_keymap(uint32_t format, int32_t fd, uint32_t size)
    {
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
            std::cerr << "unknown keymap format:" << format;
            close(fd);
            return;
        }

        char *map_str = static_cast<char *>(mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0));
        if (map_str == MAP_FAILED) {
            close(fd);
            return;
        }

        m_keymap.reset(xkb_keymap_new_from_string(m_ctx.get(), map_str, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS));
        munmap(map_str, size);
        close(fd);

        if (m_keymap)
            m_state.reset(xkb_state_new(m_keymap.get()));
        else
            m_state.reset(nullptr);
    }

    static Xkb *self()
    {
        static Xkb self;
        return &self;
    }

private:
    Xkb()
    {
        m_ctx.reset(xkb_context_new(XKB_CONTEXT_NO_FLAGS));
        if (!m_ctx) {
            std::cerr << "Failed to create xkb context" << std::endl;
            return;
        }
        m_keymap.reset(xkb_keymap_new_from_names(m_ctx.get(), nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS));
        if (!m_keymap) {
            std::cerr << "Failed to create the keymap" << std::endl;
            return;
        }
        m_state.reset(xkb_state_new(m_keymap.get()));
        if (!m_state) {
            std::cerr << "Failed to create the xkb state" << std::endl;
            return;
        }
    }

    ScopedXKBContext m_ctx;
    ScopedXKBKeymap m_keymap;
    ScopedXKBState m_state;
};

static const struct wl_registry_listener registry_listener = {
    .global = WaylandVirtualKeyboard::registry_global,
    .global_remove = WaylandVirtualKeyboard::registry_global_remove,
};

// Basic US QWERTY keymap
static const char keymap_str[] =
    "xkb_keymap {\n"
    "xkb_keycodes  { include \"evdev+aliases(qwerty)\" };\n"
    "xkb_types     { include \"complete\" };\n"
    "xkb_compat    { include \"complete\" };\n"
    "xkb_symbols   { include \"pc+us+inet(evdev)\" };\n"
    "xkb_geometry  { include \"pc(pc104)\" };\n"
    "};\n";

WaylandVirtualKeyboard::WaylandVirtualKeyboard()
    : display(nullptr), registry(nullptr), seat(nullptr), 
      keyboard_manager(nullptr), virtual_keyboard(nullptr) {
}

WaylandVirtualKeyboard::~WaylandVirtualKeyboard() {
    cleanup();
}

bool WaylandVirtualKeyboard::init() {
    display = wl_display_connect(nullptr);
    if (!display) {
        std::cerr << "Failed to connect to Wayland display" << std::endl;
        return false;
    }

    registry = wl_display_get_registry(display);
    if (!registry) {
        std::cerr << "Failed to get Wayland registry" << std::endl;
        cleanup();
        return false;
    }

    wl_registry_add_listener(registry, &registry_listener, this);
    wl_display_roundtrip(display);

    if (!keyboard_manager) {
        std::cerr << "Compositor does not support virtual-keyboard protocol" << std::endl;
        cleanup();
        return false;
    }

    // Create virtual keyboard
    virtual_keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(keyboard_manager, seat);
    if (!virtual_keyboard) {
        std::cerr << "Failed to create virtual keyboard" << std::endl;
        cleanup();
        return false;
    }

    if (!setup_keymap()) {
        std::cerr << "Failed to setup keymap" << std::endl;
        cleanup();
        return false;
    }

    wl_display_roundtrip(display);
    std::cout << "Wayland Virtual Keyboard initialized successfully" << std::endl;
    return true;
}

void WaylandVirtualKeyboard::cleanup() {
    if (virtual_keyboard) {
        zwp_virtual_keyboard_v1_destroy(virtual_keyboard);
        virtual_keyboard = nullptr;
    }
    if (keyboard_manager) {
        zwp_virtual_keyboard_manager_v1_destroy(keyboard_manager);
        keyboard_manager = nullptr;
    }
    if (seat) {
        wl_seat_destroy(seat);
        seat = nullptr;
    }
    if (registry) {
        wl_registry_destroy(registry);
        registry = nullptr;
    }
    if (display) {
        wl_display_disconnect(display);
        display = nullptr;
    }
}

bool WaylandVirtualKeyboard::setup_keymap() {
    if (!virtual_keyboard) return false;

    size_t keymap_size = strlen(keymap_str) + 1;
    
    // Create shared memory file
    int fd = memfd_create("keymap", MFD_CLOEXEC);
    if (fd < 0) {
        std::cerr << "Failed to create memfd" << std::endl;
        return false;
    }

    if (ftruncate(fd, keymap_size) < 0) {
        std::cerr << "Failed to resize memfd" << std::endl;
        close(fd);
        return false;
    }

    void* data = mmap(nullptr, keymap_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        std::cerr << "Failed to mmap keymap" << std::endl;
        close(fd);
        return false;
    }

    strcpy(static_cast<char*>(data), keymap_str);
    munmap(data, keymap_size);

    // Send keymap to compositor
    zwp_virtual_keyboard_v1_keymap(virtual_keyboard, XKB_KEYMAP_FORMAT_TEXT_V1, fd, keymap_size); // XKB_KEYMAP_FORMAT_TEXT_V1 = 1
    Xkb::self()->keyboard_keymap(XKB_KEYMAP_FORMAT_TEXT_V1, fd, keymap_size);
    close(fd);

    return true;
}

void WaylandVirtualKeyboard::registry_global(void* data, struct wl_registry* registry,
                                            uint32_t name, const char* interface, uint32_t version) {
    WaylandVirtualKeyboard* self = static_cast<WaylandVirtualKeyboard*>(data);
    
    if (strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
        self->keyboard_manager = static_cast<struct zwp_virtual_keyboard_manager_v1*>(
            wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        self->seat = static_cast<struct wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, 1));
    }
}

void WaylandVirtualKeyboard::registry_global_remove(void* data, struct wl_registry* registry, uint32_t name) {
    // Handle global removal if needed
}

void WaylandVirtualKeyboard::send_key(uint32_t time, uint32_t key, uint32_t state) {
    if (virtual_keyboard) {
        zwp_virtual_keyboard_v1_key(virtual_keyboard, time, key, state);
        wl_display_flush(display);
    }
}

// https://github.com/KDE/xdg-desktop-portal-kde/blob/master/src/waylandintegration.cpp#L563
void WaylandVirtualKeyboard::send_keysym(uint32_t time, uint32_t keysym, uint32_t state) {
    if (virtual_keyboard) {
        auto keycode = Xkb::self()->keycodeFromKeysym(keysym);
        if (!keycode) {
            std::cerr << "Failed to convert keysym into keycode" << keysym;
            return;
        }

        auto sendKey = [this, state, time](int keycode) {
            if (state) {
                zwp_virtual_keyboard_v1_key(virtual_keyboard, time, keycode, WL_KEYBOARD_KEY_STATE_PRESSED);
            } else {
                zwp_virtual_keyboard_v1_key(virtual_keyboard, time, keycode, WL_KEYBOARD_KEY_STATE_RELEASED);
            }
        };
		// The level is always 0 with KDEConnect but I assume this is here for a reason
        switch (keycode->level) {
            case 0:
                break;
            case 1:
                sendKey(KEY_LEFTSHIFT);
                break;
            case 2:
                sendKey(KEY_RIGHTALT);
                break;
            default:
                std::cerr << "Unsupported key level" << keycode->level;
                break;
        }
        sendKey(keycode->code);
        wl_display_flush(display);
    }
}

void WaylandVirtualKeyboard::send_modifiers(uint32_t mods_depressed, uint32_t mods_latched, 
                                          uint32_t mods_locked, uint32_t group) {
    if (virtual_keyboard) {
        zwp_virtual_keyboard_v1_modifiers(virtual_keyboard, mods_depressed, 
                                        mods_latched, mods_locked, group);
        wl_display_flush(display);
    }
} 