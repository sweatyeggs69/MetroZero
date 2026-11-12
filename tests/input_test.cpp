// Headless checks for controller input: a virtual SDL gamepad is attached and driven, and the
// GBA button mask from input::Manager is compared with what the default bindings promise
// (README "Controls" table). Keyboard state cannot be faked headless, so keys are checked
// through the binding table only.
#include <SDL3/SDL.h>

#include <cstdio>
#include <map>
#include <string>

#include "app/states.h"
#include "options/input.h"

using namespace metrozero::input;

static int failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

namespace {
constexpr std::uint16_t bit(Button b) { return std::uint16_t(1u << int(b)); }

SDL_Joystick* g_joy = nullptr;

void settle() {
    SDL_UpdateJoysticks();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {}
}
void press(SDL_GamepadButton b, bool down) {
    SDL_SetJoystickVirtualButton(g_joy, b, down);
    settle();
}
void tilt(SDL_GamepadAxis a, Sint16 v) {
    SDL_SetJoystickVirtualAxis(g_joy, a, v);
    settle();
}
void release_all() {
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) SDL_SetJoystickVirtualButton(g_joy, b, false);
    // A virtual pad's raw trigger range is -32768 (released) to 32767 (pulled); 0 is half way.
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a)
        SDL_SetJoystickVirtualAxis(g_joy, a, a >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? -32768 : 0);
    settle();
}

bool has_key(const Config& c, Button b, SDL_Scancode sc) {
    for (const auto& x : c.buttons[int(b)])
        if (x.kind == Binding::Kind::Key && x.code == int(sc)) return true;
    return false;
}

void keyboard_defaults() {
    const Config c = default_config();
    CHECK(has_key(c, Button::Up, SDL_SCANCODE_UP));
    CHECK(has_key(c, Button::Down, SDL_SCANCODE_DOWN));
    CHECK(has_key(c, Button::Left, SDL_SCANCODE_LEFT));
    CHECK(has_key(c, Button::Right, SDL_SCANCODE_RIGHT));
    CHECK(has_key(c, Button::A, SDL_SCANCODE_X));
    CHECK(has_key(c, Button::B, SDL_SCANCODE_Z));
    CHECK(has_key(c, Button::L, SDL_SCANCODE_A));
    CHECK(has_key(c, Button::R, SDL_SCANCODE_S));
    CHECK(has_key(c, Button::Start, SDL_SCANCODE_RETURN));
    CHECK(has_key(c, Button::Select, SDL_SCANCODE_BACKSPACE));
    // No key may drive two GBA buttons.
    std::map<int, int> uses;
    for (const auto& list : c.buttons)
        for (const auto& b : list)
            if (b.kind == Binding::Kind::Key) ++uses[b.code];
    for (const auto& [code, n] : uses) CHECK(n == 1);

    // Save state on Page Up, load on Page Down; neither drives a GBA button.
    auto first_key = [&](Hotkey h) {
        for (const auto& chord : c.hotkeys[int(h)])
            if (chord.size() == 1 && chord[0].kind == Binding::Kind::Key) return chord[0].code;
        return -1;
    };
    CHECK(first_key(Hotkey::SaveState) == SDL_SCANCODE_PAGEUP);
    CHECK(first_key(Hotkey::LoadState) == SDL_SCANCODE_PAGEDOWN);
    CHECK(!uses.count(SDL_SCANCODE_PAGEUP) && !uses.count(SDL_SCANCODE_PAGEDOWN));
    CHECK(display_name(Binding{Binding::Kind::Key, SDL_SCANCODE_PAGEUP, 0}) == "Page Up");
    CHECK(display_name(Binding{Binding::Kind::Key, SDL_SCANCODE_PAGEDOWN, 0}) == "Page Down");
    // The stored form reads back (settings.ini round trip).
    Chord parsed;
    CHECK(chord_from_string(to_string(Chord{Binding{Binding::Kind::Key, SDL_SCANCODE_PAGEUP, 0}}), parsed) &&
          parsed.size() == 1 && parsed[0].code == SDL_SCANCODE_PAGEUP);

    // Settings written with the old F5/F8 defaults pick up the new keys; custom keys stay.
    std::map<std::string, std::string> ini;
    metrozero::SettingsIO io;
    io.get = [&](const std::string& k, const std::string& fb) {
        auto it = ini.find(k);
        return it == ini.end() ? fb : it->second;
    };
    ini["input.hotkey.save_state"] = "key:F5 | pad:back+pad:rightshoulder";
    ini["input.hotkey.load_state"] = "key:F8 | pad:back+pad:leftshoulder";
    Config loaded = load(io);
    CHECK(loaded.hotkeys[int(Hotkey::SaveState)] == c.hotkeys[int(Hotkey::SaveState)]);
    CHECK(loaded.hotkeys[int(Hotkey::LoadState)] == c.hotkeys[int(Hotkey::LoadState)]);
    ini["input.hotkey.save_state"] = "key:F5";
    loaded = load(io);
    CHECK(loaded.hotkeys[int(Hotkey::SaveState)].size() == 1 &&
          loaded.hotkeys[int(Hotkey::SaveState)][0][0].code == SDL_SCANCODE_F5);
}

void gamepad(Manager& m) {
    CHECK(m.gamepad_count() == 1);
    CHECK(m.poll() == 0);

    const struct { SDL_GamepadButton pad; Button gba; } buttons[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, Button::A},          {SDL_GAMEPAD_BUTTON_NORTH, Button::A},
        {SDL_GAMEPAD_BUTTON_EAST, Button::B},           {SDL_GAMEPAD_BUTTON_WEST, Button::B},
        {SDL_GAMEPAD_BUTTON_START, Button::Start},      {SDL_GAMEPAD_BUTTON_BACK, Button::Select},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, Button::L},  {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, Button::R},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, Button::Up},       {SDL_GAMEPAD_BUTTON_DPAD_DOWN, Button::Down},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, Button::Left},   {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, Button::Right},
    };
    for (const auto& t : buttons) {
        press(t.pad, true);
        if (m.poll() != bit(t.gba))
            std::fprintf(stderr, "pad button %d -> mask %03x, want %s\n", int(t.pad), m.poll(), name(t.gba));
        CHECK(m.poll() == bit(t.gba));
        press(t.pad, false);
        CHECK(m.poll() == 0);
    }

    // Left stick past the dead zone moves; a small nudge does not.
    tilt(SDL_GAMEPAD_AXIS_LEFTX, 32767);
    CHECK(m.poll() == bit(Button::Right));
    tilt(SDL_GAMEPAD_AXIS_LEFTX, -32768);
    CHECK(m.poll() == bit(Button::Left));
    tilt(SDL_GAMEPAD_AXIS_LEFTX, 6000);
    CHECK(m.poll() == 0);
    tilt(SDL_GAMEPAD_AXIS_LEFTX, 0);
    tilt(SDL_GAMEPAD_AXIS_LEFTY, -32768);
    CHECK(m.poll() == bit(Button::Up));
    tilt(SDL_GAMEPAD_AXIS_LEFTY, 32767);
    CHECK(m.poll() == bit(Button::Down));
    // Full-tilt diagonal (about 70% on each axis) aims diagonally.
    tilt(SDL_GAMEPAD_AXIS_LEFTY, -23170);
    tilt(SDL_GAMEPAD_AXIS_LEFTX, 23170);
    CHECK(m.poll() == (bit(Button::Up) | bit(Button::Right)));
    release_all();

    // Triggers act as L / R.
    tilt(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767);
    CHECK(m.poll() == bit(Button::L));
    tilt(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
    tilt(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
    CHECK(m.poll() == bit(Button::R));
    release_all();

    // Held together, as in play: run right, aim with R, charge with B, jump with A.
    press(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true);
    press(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    press(SDL_GAMEPAD_BUTTON_EAST, true);
    press(SDL_GAMEPAD_BUTTON_SOUTH, true);
    CHECK(m.poll() == (bit(Button::Right) | bit(Button::R) | bit(Button::B) | bit(Button::A)));
    release_all();

    // D-pad and stick in opposite directions cancel like real hardware.
    press(SDL_GAMEPAD_BUTTON_DPAD_LEFT, true);
    tilt(SDL_GAMEPAD_AXIS_LEFTX, 32767);
    CHECK(m.poll() == 0);
    release_all();
}

void shortcuts(Manager& m) {
    int saves = 0, loads = 0;
    metrozero::states::Hotkeys hk;
    hk.quick_save = [&] { ++saves; };
    hk.quick_load = [&] { ++loads; };
    metrozero::states::set_hotkeys(hk);

    // Back + Right Bumper saves, and neither Select nor R reaches the game while held.
    press(SDL_GAMEPAD_BUTTON_BACK, true);
    SDL_Event e{};
    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    e.gbutton.which = SDL_GetJoystickID(g_joy);
    e.gbutton.button = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    press(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    CHECK(m.handle_event(e));
    CHECK(saves == 1 && loads == 0);
    CHECK((m.poll() & (bit(Button::R) | bit(Button::Select))) == 0);
    release_all();
    CHECK(m.poll() == 0);
    // Released buttons work normally again.
    press(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    CHECK(m.poll() == bit(Button::R));
    release_all();
    metrozero::states::set_hotkeys({});
}
void binding_strings() {
    const Binding samples[] = {
        {Binding::Kind::JoyButton, 12, 0},         {Binding::Kind::JoyAxis, 4, -1},
        {Binding::Kind::JoyAxis, 0, 1},            {Binding::Kind::JoyHat, 1, SDL_HAT_LEFT},
        {Binding::Kind::Mouse, SDL_BUTTON_X1, 0},  {Binding::Kind::Mouse, SDL_BUTTON_MIDDLE, 0},
        {Binding::Kind::PadButton, SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, 0},
    };
    for (const auto& b : samples) {
        Binding back;
        CHECK(from_string(to_string(b), back) && back == b);
    }
    CHECK(to_string(samples[0]) == "joy:button12");
    CHECK(display_name(samples[0]) == "Joystick Button 13");
    CHECK(display_name(samples[3]) == "Joystick Hat 2 Left");
    CHECK(display_name(samples[4]) == "Mouse Button 4");
    Binding bad;
    CHECK(!from_string("mouse:left", bad));  // left/right clicks stay with the menus
    CHECK(!from_string("joy:hat0north", bad));
    CHECK(!from_string("joy:button", bad));
    // Shortcuts keep to keys and standard controller buttons.
    Chord c;
    CHECK(!chord_from_string("joy:button3", c));

    // Extra actions start unbound, round-trip through settings, and can be cleared with "none".
    const Config d = default_config();
    for (const auto& list : d.actions) CHECK(list.empty());
    CHECK(!d.right_stick_aim);
    std::map<std::string, std::string> ini;
    metrozero::SettingsIO io;
    io.get = [&](const std::string& k, const std::string& fb) {
        auto it = ini.find(k);
        return it == ini.end() ? fb : it->second;
    };
    io.set = [&](const std::string& k, const std::string& v) { ini[k] = v; };
    Config c2 = d;
    c2.actions[int(Action::MissileToggle)] = {samples[0], samples[6]};
    c2.right_stick_aim = true;
    store(c2, io);
    CHECK(ini["input.action.missile_toggle"] == "joy:button12 | pad:paddle4");
    CHECK(ini["input.action.aim_up"] == "none");
    const Config loaded = load(io);
    CHECK(loaded.actions == c2.actions);
    CHECK(loaded.right_stick_aim);
}

// Extra actions, driven through poll_frame() the way the game loop calls it.
void actions(std::map<std::string, std::string>& ini, Manager& m) {
    ini["input.action.missile_toggle"] = "pad:paddle1";
    ini["input.action.aim_up"] = "pad:paddle2";
    ini["input.action.aim_down"] = "pad:paddle3";
    m.reload();
    CHECK(m.poll_frame() == 0);

    // One press arms missiles (R held), the next press disarms.
    press(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, true);
    CHECK(m.poll_frame() == bit(Button::R));
    CHECK(m.poll_frame() == bit(Button::R));  // holding does not toggle again
    press(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, false);
    CHECK(m.poll_frame() == bit(Button::R));
    CHECK(m.missiles_armed());
    press(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, true);
    CHECK(m.poll_frame() == 0);
    press(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, false);
    CHECK(m.poll_frame() == 0);
    CHECK(!m.missiles_armed());
    // The paddle itself is not a GBA button.
    CHECK(m.poll() == 0);

    // Aim up holds L and drops Down; aim down holds L + Down and drops Up.
    press(SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, true);
    CHECK(m.poll_frame() == bit(Button::L));
    press(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
    CHECK(m.poll_frame() == bit(Button::L));
    press(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
    press(SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, false);
    CHECK(m.poll_frame() == 0);
    press(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, true);
    // L alone first, so the game is aiming diagonally before Down arrives and Samus doesn't crouch.
    CHECK(m.poll_frame() == bit(Button::L));
    CHECK(m.poll_frame() == bit(Button::L));
    CHECK(m.poll_frame() == (bit(Button::L) | bit(Button::Down)));
    press(SDL_GAMEPAD_BUTTON_DPAD_UP, true);
    CHECK(m.poll_frame() == (bit(Button::L) | bit(Button::Down)));
    press(SDL_GAMEPAD_BUTTON_DPAD_UP, false);
    // Straight from aiming down to aiming up: one frame of Up resets the game's diagonal.
    press(SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, true);
    press(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, false);
    CHECK(m.poll_frame() == (bit(Button::L) | bit(Button::Up)));
    CHECK(m.poll_frame() == bit(Button::L));
    release_all();
    CHECK(m.poll_frame() == 0);

    // Right stick aiming is off by default, then aims up or down when turned on.
    tilt(SDL_GAMEPAD_AXIS_RIGHTY, -32768);
    CHECK(m.poll_frame() == 0);
    ini["input.right_stick_aim"] = "true";
    m.reload();
    CHECK(m.poll_frame() == bit(Button::L));
    tilt(SDL_GAMEPAD_AXIS_RIGHTY, 32767);  // straight from up: already diagonal, Down right away
    CHECK(m.poll_frame() == (bit(Button::L) | bit(Button::Down)));
    tilt(SDL_GAMEPAD_AXIS_RIGHTY, 5000);  // inside the dead zone
    CHECK(m.poll_frame() == 0);
    release_all();

    // Rebinding an action takes the next controller button and frees it from GBA buttons.
    m.start_remap(Action::MissileToggle);
    CHECK(m.remapping() && m.action_remap_target() == Action::MissileToggle);
    SDL_Event e{};
    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    e.gbutton.which = SDL_GetJoystickID(g_joy);
    e.gbutton.button = SDL_GAMEPAD_BUTTON_NORTH;
    CHECK(m.handle_event(e));
    CHECK(!m.remapping());
    CHECK(m.describe(Action::MissileToggle) == "Y Button");
    CHECK(ini["input.action.missile_toggle"] == "pad:y");
    CHECK(ini["input.A"].find("pad:y") == std::string::npos);
    m.clear(Action::MissileToggle);
    CHECK(m.describe(Action::MissileToggle) == "None");
    m.reset_to_defaults();
}

// A controller SDL has no layout for is read as a raw joystick, and its buttons and hat bind
// by number.
void raw_joystick(Manager& m, SDL_Joystick* raw) {
    CHECK(m.gamepad_count() == 2);
    m.start_remap(Button::A);
    SDL_Event e{};
    e.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
    e.jbutton.which = SDL_GetJoystickID(raw);
    e.jbutton.button = 9;
    CHECK(m.handle_event(e));
    CHECK(!m.remapping());
    CHECK(m.describe(Button::A).find("Joystick Button 10") != std::string::npos);
    SDL_SetJoystickVirtualButton(raw, 9, true);
    settle();
    CHECK(m.poll() == bit(Button::A));
    SDL_SetJoystickVirtualButton(raw, 9, false);
    settle();
    CHECK(m.poll() == 0);

    m.start_remap(Button::Up);
    e = {};
    e.type = SDL_EVENT_JOYSTICK_HAT_MOTION;
    e.jhat.which = SDL_GetJoystickID(raw);
    e.jhat.hat = 0;
    e.jhat.value = SDL_HAT_LEFTUP;
    CHECK(m.handle_event(e));
    CHECK(m.describe(Button::Up).find("Joystick Hat 1 Up") != std::string::npos);
    SDL_SetJoystickVirtualHat(raw, 0, SDL_HAT_RIGHTUP);
    settle();
    CHECK(m.poll() == bit(Button::Up));
    SDL_SetJoystickVirtualHat(raw, 0, SDL_HAT_CENTERED);
    settle();

    // A standard pad's own buttons still bind by name, not as raw joystick buttons.
    m.start_remap(Button::B);
    e = {};
    e.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
    e.jbutton.which = SDL_GetJoystickID(g_joy);
    e.jbutton.button = 0;
    CHECK(m.handle_event(e));
    CHECK(m.remapping());  // ignored, still waiting
    m.cancel_remap();
    m.reset_to_defaults();
}
}  // namespace

int main() {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    keyboard_defaults();

    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "MetroZero test pad";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (!id || !(g_joy = SDL_OpenJoystick(id))) {
        std::fprintf(stderr, "virtual gamepad: %s\n", SDL_GetError());
        return 1;
    }
    release_all();
    binding_strings();
    {
        std::map<std::string, std::string> store;
        metrozero::SettingsIO io;
        io.get = [&](const std::string& k, const std::string& fb) {
            auto it = store.find(k);
            return it == store.end() ? fb : it->second;
        };
        io.set = [&](const std::string& k, const std::string& v) { store[k] = v; };
        io.save = [] {};
        Manager m(io);
        gamepad(m);
        shortcuts(m);
        actions(store, m);
    }
    {
        SDL_VirtualJoystickDesc rd;
        SDL_INIT_INTERFACE(&rd);
        rd.type = SDL_JOYSTICK_TYPE_UNKNOWN;
        rd.naxes = 2;
        rd.nbuttons = 16;
        rd.nhats = 1;
        rd.name = "MetroZero test arcade stick";
        const SDL_JoystickID rid = SDL_AttachVirtualJoystick(&rd);
        SDL_Joystick* raw = rid ? SDL_OpenJoystick(rid) : nullptr;
        CHECK(raw && !SDL_IsGamepad(rid));
        settle();
        if (raw) {
            std::map<std::string, std::string> store;
            metrozero::SettingsIO io;
            io.get = [&](const std::string& k, const std::string& fb) {
                auto it = store.find(k);
                return it == store.end() ? fb : it->second;
            };
            io.set = [&](const std::string& k, const std::string& v) { store[k] = v; };
            io.save = [] {};
            Manager m(io);
            raw_joystick(m, raw);
            SDL_CloseJoystick(raw);
        }
        if (rid) SDL_DetachVirtualJoystick(rid);
    }
    SDL_CloseJoystick(g_joy);
    SDL_DetachVirtualJoystick(id);
    SDL_Quit();
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    else std::printf("input: all checks passed\n");
    return failures ? 1 : 0;
}
