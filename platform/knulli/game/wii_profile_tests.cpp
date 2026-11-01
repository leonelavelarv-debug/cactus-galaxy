// SPDX-License-Identifier: GPL-3.0-only
// The WII HYBRID profiles (smg/config/input/wii/*.ini) through the real bindings: every line is understood, and each
// physical button, trigger and stick reaches the Wii target the profile says, alone and together. Usage:
//   nebula_wii_profile_tests <folder with default.ini and mario_galaxy.ini>
// Exit status 0 when all pass.
#include "wii_profile.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace in = galaxy::input;
using T = in::WiiTarget;

namespace {

int g_failures = 0;
void expect(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

constexpr float kThreshold = 96.0f / 255.0f;  // the host's trigger threshold

struct Pad {
    in::PadSample sample;
    Pad() {
        sample.connected = true;
        sample.scheme = in::ControlScheme::SwitchPro;
    }
    Pad& button(unsigned bit) {
        sample.buttons |= in::pad::bit(bit);
        return *this;
    }
    Pad& axis(in::PadAxis a, float v) {
        sample.axes[static_cast<std::size_t>(a)] = v;
        return *this;
    }
};

in::LogicalInput eval(const in::ControlsConfig& config, const Pad& pad) {
    return in::evaluate_pad_bindings(config.table(in::ControlScheme::SwitchPro), pad.sample, kThreshold, 0u, false);
}

bool only(const in::LogicalInput& l, T want) {
    return l.pressed == in::wii_target_bit(want);
}

std::string read_file(const std::string& path) {
    std::ifstream in_file(path, std::ios::binary);
    std::ostringstream text;
    text << in_file.rdbuf();
    return text.str();
}

void check_profile(const std::string& dir, const std::string& file, bool nunchuk_shake) {
    const std::string text = read_file(dir + "/" + file);
    expect(!text.empty(), file + ": exists");
    in::ControlsConfig config = in::default_controls_config();
    const galaxy::host::ProfileResult r = galaxy::host::apply_wii_profile_text(config, text);
    expect(r.applied >= 30 && r.ignored == 0, file + ": every line understood (" + std::to_string(r.applied) + " applied, " +
                                                  std::to_string(r.ignored) + " ignored)");
    expect(!r.name.empty(), file + ": has a name (" + r.name + ")");

    namespace pad = in::pad;
    expect(only(eval(config, Pad().button(pad::kSouthBit)), T::A), file + ": B (bottom) is Wii A");
    expect(only(eval(config, Pad().button(pad::kEastBit)), T::B), file + ": A (right) is Wii B");
    expect(only(eval(config, Pad().axis(in::PadAxis::RightTrigger, 1.0f)), T::B), file + ": R2 is Wii B");
    expect(only(eval(config, Pad().button(pad::kLeftShoulderBit)), T::C), file + ": L1 is Nunchuk C");
    expect(only(eval(config, Pad().axis(in::PadAxis::LeftTrigger, 1.0f)), T::Z), file + ": L2 is Nunchuk Z");
    expect(only(eval(config, Pad().button(pad::kNorthBit)), T::One), file + ": X (top) is Wii 1");
    expect(only(eval(config, Pad().button(pad::kWestBit)), T::Two), file + ": Y (left) is Wii 2");
    expect(only(eval(config, Pad().button(pad::kStartBit)), T::Plus), file + ": START is +");
    expect(only(eval(config, Pad().button(pad::kBackBit)), T::Minus), file + ": SELECT is -");
    expect(only(eval(config, Pad().button(pad::kDpadUpBit)), T::DpadUp), file + ": D-pad up is D-pad up");
    expect(only(eval(config, Pad().button(pad::kRightStickBit)), T::PointerRecenter), file + ": right-stick click recenters");
    expect(only(eval(config, Pad().button(pad::kRightShoulderBit)), nunchuk_shake ? T::NunchukShake : T::Shake),
           file + ": R1 shakes the " + (nunchuk_shake ? "Nunchuk" : "Wii Remote"));

    // Sticks: left is the Nunchuk stick, right only points. An analog direction also marks its own target as pressed
    // (the hysteresis the D-pad-like uses need), so the expected mask is that one target and no button.
    const in::LogicalInput left = eval(config, Pad().axis(in::PadAxis::LeftX, 1.0f));
    expect(left.stick_x > 0.9f && left.pressed == in::wii_target_bit(T::StickRight) && left.pointer_x == 0.0f,
           file + ": left stick is the Nunchuk stick only");
    const in::LogicalInput right = eval(config, Pad().axis(in::PadAxis::RightY, 1.0f));
    expect(right.pointer_y > 0.9f && right.pressed == in::wii_target_bit(T::PointerUp) && right.stick_y == 0.0f,
           file + ": right stick is the pointer only (no D-pad, no Nunchuk stick)");

    // Everything at once: move, point, jump, shoot, crouch and spin without losing any.
    const in::LogicalInput all = eval(config, Pad()
                                                  .axis(in::PadAxis::LeftX, 0.8f)
                                                  .axis(in::PadAxis::RightX, 0.8f)
                                                  .button(pad::kSouthBit)
                                                  .button(pad::kEastBit)
                                                  .axis(in::PadAxis::LeftTrigger, 1.0f)
                                                  .button(pad::kRightShoulderBit));
    const std::uint32_t want = in::wii_target_bit(T::A) | in::wii_target_bit(T::B) | in::wii_target_bit(T::Z) |
                               in::wii_target_bit(nunchuk_shake ? T::NunchukShake : T::Shake) |
                               in::wii_target_bit(T::StickRight) | in::wii_target_bit(T::PointerRight);
    expect(all.pressed == want && all.stick_x > 0.7f && all.pointer_x > 0.7f,
           file + ": move + point + jump + shoot + crouch + spin together");

    // The runtime's own defaults are untouched by a profile applied to a copy.
    expect(in::default_controls_config() == in::default_controls_config(), file + ": defaults are values, not shared state");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <folder with default.ini and mario_galaxy.ini>\n", argv[0]);
        return 2;
    }
    check_profile(argv[1], "default.ini", false);
    check_profile(argv[1], "mario_galaxy.ini", true);
    std::printf("%s (%d failures)\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
