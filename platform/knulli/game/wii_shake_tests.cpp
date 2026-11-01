// SPDX-License-Identifier: GPL-3.0-only
// The handheld's shake button (wii_shake.h) against the game's swing detector, simulated: reports at 100 a second
// filling a history, frames at 60 a second asking WPadHVSwing::updateSwing's question (newest sample 1 g or more from the
// one 20 back). Checks: a tap is the runtime's pulse sample for sample (native_input.cpp advance_wiimote_shake_accel);
// holding the button without repeat swings only briefly (the old behaviour, why the vine needed tapping); holding with
// repeat swings on every frame, for the Wii Remote and the Nunchuk calibrations. Exit status 0 when all pass.
#include "wii_shake.h"

#include "galaxy/native_input.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace in = galaxy::input;
using galaxy::host::ShakeButton;
using galaxy::host::ShakeCounts;

namespace {

int g_failures = 0;
void expect(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

// Counts per g on each axis and the resting reading: the virtual Wii Remote's EEPROM calibration (0G 0x200, 1G 0x264:
// 100 counts) and the virtual Nunchuk's registers (wii_device_data.cpp: 0G/1G 0x7d/0xb1, 0x7e/0xaf, 0x7a/0xae, x4).
struct Device {
    const char* name;
    float per_g[3];
    ShakeCounts rest;
};
constexpr Device kRemote{"Wii Remote", {100.0f, 100.0f, 100.0f},
                         {in::kWiimoteRestAccelX, in::kWiimoteRestAccelY, in::kWiimoteRestAccelZ}};
constexpr Device kNunchuk{"Nunchuk", {208.0f, 196.0f, 208.0f}, {512, 512, 512}};

// `held` per report (100 a second); returns, per frame (60 a second), whether the game would see a swing.
std::vector<bool> swings(const Device& d, const std::vector<bool>& held, bool hold_repeats) {
    ShakeButton button;
    std::vector<ShakeCounts> history;
    std::vector<bool> frames;
    std::size_t next_frame_report = 0;
    for (std::size_t r = 0; r < held.size(); ++r) {
        ShakeCounts c = d.rest;
        ShakeCounts s{};
        if (button.advance(held[r], 12, hold_repeats, s)) c = s;
        history.push_back(c);
        // A frame every 100/60 reports (frame f at report 5f/3) reads the newest sample.
        while (next_frame_report * 5 <= r * 3) {
            const ShakeCounts& now = history.back();
            bool swing = false;
            if (history.size() > galaxy::host::kSwingLagSamples) {
                const ShakeCounts& past = history[history.size() - 1 - galaxy::host::kSwingLagSamples];
                const float dx = (static_cast<float>(now.x) - past.x) / d.per_g[0];
                const float dy = (static_cast<float>(now.y) - past.y) / d.per_g[1];
                const float dz = (static_cast<float>(now.z) - past.z) / d.per_g[2];
                swing = std::sqrt(dx * dx + dy * dy + dz * dz) >= 1.0f;
            }
            frames.push_back(swing);
            ++next_frame_report;
        }
    }
    return frames;
}

std::size_t count(const std::vector<bool>& v, std::size_t from = 0) {
    std::size_t n = 0;
    for (std::size_t i = from; i < v.size(); ++i) n += v[i] ? 1u : 0u;
    return n;
}

std::vector<bool> pattern(std::size_t rest_before, std::size_t held_reports, std::size_t total) {
    std::vector<bool> v(total, false);
    for (std::size_t i = rest_before; i < rest_before + held_reports && i < total; ++i) v[i] = true;
    return v;
}

}  // namespace

int main() {
    // 1. A tap (and a hold without repeat) is the runtime's pulse, reading for reading.
    for (const std::size_t held_reports : {1u, 5u, 12u, 30u, 300u}) {
        const std::vector<bool> held = pattern(25, held_reports, 400);
        ShakeButton button;
        in::WiimoteInputSnapshot snapshot{};
        bool previous = false;
        std::uint8_t remaining = 0;
        bool same = true;
        for (const bool h : held) {
            in::WiimoteInputSnapshot runtime{};
            const bool r_active = in::advance_wiimote_shake_accel(runtime, h, previous, remaining);
            ShakeCounts c{};
            const bool b_active = button.advance(h, 12, false, c);
            if (r_active != b_active ||
                (r_active && (runtime.accel.x != c.x || runtime.accel.y != c.y || runtime.accel.z != c.z))) {
                same = false;
            }
        }
        (void)snapshot;
        expect(same, "held " + std::to_string(held_reports) + " reports without repeat = the runtime's pulse, sample for sample");
    }

    for (const Device& d : {kRemote, kNunchuk}) {
        const std::string dev = d.name;
        // 2. A tap: the game sees a short swing (as before).
        const std::vector<bool> tap = swings(d, pattern(50, 3, 300), true);
        const std::size_t tap_frames = count(tap);
        const std::vector<bool> tap_old = swings(d, pattern(50, 3, 300), false);
        expect(tap_frames > 0 && tap_frames == count(tap_old),
               dev + ": a tap swings " + std::to_string(tap_frames) + " frames, as before");

        // 3. Holding 3 s (300 reports): without repeat the swing ends early; with repeat it lasts the whole hold.
        const std::vector<bool> old_hold = swings(d, pattern(50, 300, 400), false);
        const std::vector<bool> new_hold = swings(d, pattern(50, 300, 400), true);
        // Frames while held: reports 50..349 = frames 30..209; skip the first frame of the press.
        std::size_t first = 31, last = 209, held_frames = last - first + 1;
        std::size_t old_in = 0, new_in = 0;
        for (std::size_t f = first; f <= last && f < new_hold.size(); ++f) {
            old_in += old_hold[f] ? 1u : 0u;
            new_in += new_hold[f] ? 1u : 0u;
        }
        std::printf("     %s, held 3 s: swing on %zu of %zu frames before, %zu of %zu now\n", d.name, old_in, held_frames,
                    new_in, held_frames);
        expect(old_in < held_frames / 4, dev + ": holding without repeat stops counting (why the vine needed tapping)");
        expect(new_in == held_frames, dev + ": holding with repeat swings on every frame of the hold");
        // And it stops after release: a few frames of the 20-sample lag, then nothing.
        expect(count(new_hold, 240) == 0, dev + ": no swing once released and the lag has passed");
    }
    std::printf("%s (%d failures)\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
