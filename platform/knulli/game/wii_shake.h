// SPDX-License-Identifier: GPL-3.0-only
// The handheld's shake: a button stands for shaking the Wii Remote or the Nunchuk. The game sees a shake through
// WPadHVSwing::updateSwing (native/petari src/Game/System/WPadHVSwing.cpp): every frame it compares the newest
// accelerometer sample with the one 20 samples back (one sample per report, 100 a second) and calls it a swing when they
// are 1 g or more apart. MR::isPadSwing (either device) is a level the game reads every frame: the Sproutle vine
// (Ride/Creeper.cpp) and the beanstalk (Ride/Plant.cpp) climb while it is true, adding speed each frame.
//
// A tap is the runtime's pulse (native_input.cpp advance_wiimote_shake_accel): `tap_reports` reports alternating between
// two readings far from rest. Holding the button keeps shaking: 20 reports alternating, 20 at rest, again; with that
// period every sample is 1 g or more from the one 20 back, so the game sees a swing on every frame for as long as the
// button is held. (A shake that only alternated would match the sample 20 back -- an even lag -- and stop counting.)
// These are synthetic readings, not a motion sensor: the handheld has none.
#pragma once

#include <cstdint>

namespace galaxy::host {

struct ShakeCounts {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t z = 0;
};

// The two readings of the runtime's pulse (the same for the Wii Remote and the Nunchuk).
[[nodiscard]] constexpr ShakeCounts shake_reading(bool odd) noexcept {
    return odd ? ShakeCounts{0x02f0u, 0x0200u, 0x0130u} : ShakeCounts{0x0110u, 0x0200u, 0x02d0u};
}

inline constexpr std::uint16_t kSwingLagSamples = 20;  // WPadHVSwing::updateSwing's getPastAcceleration(…, 20, …)

class ShakeButton {
public:
    // One report. Returns true and the reading to report while shaking; false: leave the accelerometer as it is.
    bool advance(bool held, std::uint8_t tap_reports, bool hold_repeats, ShakeCounts& out) noexcept {
        if (held && !previous_) {
            reports_ = 0;
            tap_left_ = tap_reports;
        }
        previous_ = held;
        bool shaking = false;
        if (held && hold_repeats && reports_ >= tap_reports) {
            shaking = (reports_ % (2u * kSwingLagSamples)) < kSwingLagSamples;
        } else if (tap_left_ != 0) {
            shaking = true;
            --tap_left_;
        }
        if (shaking) {
            // The runtime's pulse counts down from tap_reports and alternates on the count's parity; counting up from 0
            // with tap_reports even gives the same sequence.
            out = shake_reading(((tap_reports - reports_) & 1u) != 0u);
        }
        if (held || tap_left_ != 0 || shaking) ++reports_;
        return shaking;
    }

private:
    bool previous_ = false;
    std::uint32_t reports_ = 0;
    std::uint8_t tap_left_ = 0;
};

}  // namespace galaxy::host
