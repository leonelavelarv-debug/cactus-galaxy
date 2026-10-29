#pragma once
// Progress of the test hooks (GALAXY_TEST_GALAXY_TOUR) for the host window: the count of GameScene draws. A tour
// that stops counting is waiting on something the game shows outside the scene (a pause screen: "the connection with
// the Nunchuk was interrupted ... press A"); the handheld host taps A then (NEBULA_AUTO_RECONNECT=1).
#include <atomic>
#include <cstdint>

namespace galaxy {
inline std::atomic<std::uint64_t> test_scene_frame_count{0u};
}  // namespace galaxy
