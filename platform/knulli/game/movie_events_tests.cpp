// Nebula's movie timing on the port (native/petari/include/Game/Screen/NebulaMovieEvents.hpp): the events of the
// frames a game frame steps over fire once, a repeated frame fires nothing, a restart works frame by frame as on the
// console; the natural end holds until the decoded audio is played, the mixer's last block included.
#include "Game/Screen/NebulaMovieEvents.hpp"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

namespace {

int g_failures = 0;
void expect(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok   " : "FALLO", what);
    if (!ok) ++g_failures;
}

// Events (frame -> times fired) through the range tracker, frame by frame as MoviePlayingSequence::exePlay sees them.
std::map<int, int> run(NebulaMovieFrameRange& range, const std::vector<int>& frames, const std::vector<int>& events) {
    std::map<int, int> fired;
    for (int frame : frames) {
        int from = 0, to = 0;
        range.advance(frame, &from, &to);
        for (int e : events) {
            if (NebulaMovieFrameRange::contains(e, from, to)) fired[e]++;
        }
    }
    return fired;
}

// The mixer's queue as THPSimplePlayerWrapper::mixAudio walks it: 20 blocks, samples left in each; one mix pulls
// `request` samples from the output index onward, if the go-ahead allows. Returns the samples played.
struct Mixer {
    unsigned blocks[20] = {};
    int out = 0;
    unsigned mix(unsigned request, bool draining) {
        if (!nebulaMovieMixerMayMix(draining, blocks[out] != 0, blocks[(out + 1) % 20] != 0)) return 0;
        unsigned played = 0;
        while (request > 0 && blocks[out] != 0) {
            const unsigned take = blocks[out] < request ? blocks[out] : request;
            blocks[out] -= take;
            request -= take;
            played += take;
            if (blocks[out] == 0) out = (out + 1) % 20;
        }
        return played;
    }
    int queued() const {
        int n = 0;
        for (unsigned b : blocks) n += b != 0;
        return n;
    }
};

}  // namespace

int main() {
    {  // The skip of the report: 3886 -> 3888 in one game frame, PrologueA's event at 3887 fires once.
        NebulaMovieFrameRange range;
        const auto fired = run(range, {3885, 3886, 3888, 3888, 3889}, {3887});
        expect(fired.count(3887) && fired.at(3887) == 1, "3886 -> 3888: the event at 3887 fires, once");
    }
    {  // A repeated frame (the movie waiting on the disc) does not fire its event again.
        NebulaMovieFrameRange range;
        const auto fired = run(range, {10, 11, 11, 11, 12}, {11});
        expect(fired.at(11) == 1, "a repeated frame fires its event once");
    }
    {  // Each frame in turn: exactly the console's exact-frame check.
        NebulaMovieFrameRange range;
        const auto fired = run(range, {0, 1, 2, 3, 4}, {0, 2, 4});
        expect(fired.size() == 3 && fired.at(0) == 1 && fired.at(2) == 1 && fired.at(4) == 1,
               "frame by frame: each event once, as on the console");
    }
    {  // The first frame seen covers itself alone: events before it were never reached on the console either.
        NebulaMovieFrameRange range;
        const auto fired = run(range, {3, 4}, {1, 3});
        expect(!fired.count(1) && fired.at(3) == 1, "the first frame seen does not fire earlier events");
    }
    {  // A restart (the frame goes back): frame by frame again, events fire again as the movie reaches them.
        NebulaMovieFrameRange range;
        const auto fired = run(range, {100, 101, 102, 0, 1, 2, 101}, {0, 1, 101});
        expect(fired.at(0) == 1 && fired.at(1) == 1 && fired.at(101) == 2, "a restart fires its events again, once each");
    }
    {  // reset() (a new movie, MoviePlayingSequence::exePlayStart) forgets the last frame.
        NebulaMovieFrameRange range;
        run(range, {50, 51}, {});
        range.reset();
        const auto fired = run(range, {52, 53}, {51, 52});
        expect(!fired.count(51) && fired.at(52) == 1, "after reset the next frame covers itself alone");
    }
    {  // No movie (MR::getMovieCurrentFrame() == -1, a skip stopped it): nothing fires.
        NebulaMovieFrameRange range;
        const auto fired = run(range, {7, -1, -1}, {7, 8});
        expect(fired.at(7) == 1 && !fired.count(8), "no movie: nothing fires");
    }
    {  // A whole PrologueA (5591 frames) with steps of 0 to 4 frames: every event fires exactly once.
        NebulaMovieFrameRange range;
        std::vector<int> frames, events;
        std::srand(1234);
        for (int f = 0; f < 5591;) {
            frames.push_back(f);
            f += std::rand() % 5;  // 0: the same frame again
        }
        frames.push_back(5590);
        for (int e = 0; e < 5591; e += 7) events.push_back(e);
        const auto fired = run(range, frames, events);
        bool once = fired.size() == events.size();
        for (const auto& [frame, n] : fired) once = once && n == 1;
        expect(once, "a whole movie with skipped and repeated frames: every event exactly once");
    }
    {  // The drain: done when the mixer has played every decoded block, or at the cap.
        NebulaMovieDrain drain;
        expect(!drain.active(), "the drain starts inactive");
        drain.start();
        expect(drain.active() && !drain.step(3) && !drain.step(1) && drain.step(0), "the drain ends when the queue is empty");
        drain.reset();
        drain.start();
        int frames = 0;
        while (!drain.step(5)) ++frames;
        expect(frames + 1 == NebulaMovieDrain::kMaxFrames, "a mixer that stopped pulling: the drain ends at its cap");
    }
    {  // The last block: the console's mixer waits for a block behind it, forever at the end; draining it plays.
        Mixer mixer;
        mixer.blocks[0] = 1066;  // a 32 kHz movie's block, the only one left
        expect(mixer.mix(512, false) == 0 && mixer.queued() == 1, "not draining: the last block alone waits (as before)");
        unsigned played = 0;
        while (mixer.queued() > 0) {
            const unsigned got = mixer.mix(512, true);
            if (got == 0) break;
            played += got;
        }
        expect(played == 1066 && mixer.queued() == 0, "draining: the last block plays to its end");
    }
    {  // While playing, the margin stays: two blocks queued mix, one alone waits for the next.
        Mixer mixer;
        mixer.blocks[0] = 600;
        mixer.blocks[1] = 600;
        expect(mixer.mix(512, false) == 512, "two blocks: the mixer plays");
        mixer.blocks[1] = 0;
        expect(mixer.mix(512, false) == 0, "one block left while playing: it waits, as before");
    }
    {  // The end as MoviePlayerSimple runs it: the queue drains through the mixer, then the movie stops.
        Mixer mixer;
        for (int i = 0; i < 5; ++i) mixer.blocks[i] = 1066;
        NebulaMovieDrain drain;
        drain.start();
        int game_frames = 0;
        bool stopped = false;
        while (!stopped && game_frames < 200) {
            mixer.mix(534, true);  // ~1/60 s of 32 kHz audio per game frame
            stopped = drain.step(mixer.queued());
            ++game_frames;
        }
        expect(stopped && mixer.queued() == 0 && game_frames < NebulaMovieDrain::kMaxFrames,
               "natural end: every queued block is played before the stop");
    }
    std::printf("%s\n", g_failures == 0 ? "todas las pruebas pasan" : "HAY FALLOS");
    return g_failures == 0 ? 0 : 1;
}
