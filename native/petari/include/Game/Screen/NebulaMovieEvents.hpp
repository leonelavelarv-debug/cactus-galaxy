#pragma once

// Nebula (PC): the prerendered movies' timing on a port whose movie clock can move more than one frame per game frame.
// MoviePlayerSimple decodes extra frames to keep the movie's audio fed (NEBULA_VIDEO_AUDIO_RELOJ), so the movie's
// current frame can go from 3886 to 3888 in one game frame; the console's events (subtitles, the pad's rumble) fire on
// one exact frame and would be missed. These pieces are free of the game's types so that a plain test can run them
// (platform/knulli/game/movie_events_tests.cpp).

// The movie frames one game frame covers: (from, to]. An event whose frame lies there fires, once.
struct NebulaMovieFrameRange {
    enum { kNone = -0x7FFFFFFF };
    int mPrevious;

    NebulaMovieFrameRange() : mPrevious(kNone) {}

    void reset() { mPrevious = kNone; }

    // The first frame seen covers itself alone (as the console's exact check did: frames before it were never
    // reached); a frame behind the last one (the movie started again) covers itself alone too; the same frame again
    // covers nothing (an event already fired does not fire twice); no movie (a negative frame) covers nothing.
    void advance(int current, int* from, int* to) {
        if (current < 0) {
            *from = *to = current;
            return;
        }
        *from = (mPrevious == kNone || current < mPrevious) ? current - 1 : mPrevious;
        *to = current;
        mPrevious = current;
    }

    static bool contains(int frame, int from, int to) { return from < frame && frame <= to; }
};

// The end of a movie with audio: its last picture stays while the mixer plays the audio already decoded, at most
// kMaxFrames game frames (a mixer that stopped pulling must not hold the movie forever).
struct NebulaMovieDrain {
    enum { kMaxFrames = 90 };
    int mFrames;

    NebulaMovieDrain() : mFrames(-1) {}

    bool active() const { return mFrames >= 0; }
    void start() { mFrames = 0; }
    void reset() { mFrames = -1; }

    // One game frame of the drain: true when the movie may stop now.
    bool step(int queuedAudioBuffers) {
        if (queuedAudioBuffers <= 0) {
            return true;
        }
        return ++mFrames >= kMaxFrames;
    }
};

// The mixer's go-ahead: the console mixes only with another decoded block behind the current one (a margin while the
// movie plays); while the movie drains at its end there is none behind the last, which then plays alone.
inline bool nebulaMovieMixerMayMix(bool draining, bool currentValid, bool nextValid) {
    return nextValid || (draining && currentValid);
}
