#include "Game/Demo/DemoPadRumbler.hpp"
#include "Game/Util/ObjUtil.hpp"
#ifdef TARGET_PC
#include "Game/Screen/NebulaMovieEvents.hpp"
#endif
#include <cstdio>

DemoPadRumbler::DemoPadRumbler(const char* pName) : mNumPadRumbleEntries(), mPadRumbleEntries(), _8() {
    if (pName == nullptr)
        return;

    char buf[0x100];
    snprintf(buf, sizeof(buf), "MoviePadRumble%s.arc", pName);
    JMapInfo* map = MR::createCsvParser(buf, "PadRumbleData.bcsv");

    mNumPadRumbleEntries = MR::getCsvDataElementNum(map);
    PadRumbleInfo* infos = new PadRumbleInfo[mNumPadRumbleEntries];
    mPadRumbleEntries = infos;

    for (int i = 0; i < mNumPadRumbleEntries; i++) {
        PadRumbleInfo* rumble = &infos[i];
        MR::getCsvDataStrOrNULL(&rumble->mName, map, "PadRumbleName", i);
        MR::getCsvDataS32(&rumble->mStartFrame, map, "StartFrame", i);
    }
}

#ifdef TARGET_PC
// Nebula: the frames one game frame of the movie covered (it can step more than one); each rumble starts once.
void DemoPadRumbler::updateBetween(s32 from, s32 to) {
    for (int i = 0; i < mNumPadRumbleEntries; i++) {
        PadRumbleInfo* rumble = &mPadRumbleEntries[i];
        if (NebulaMovieFrameRange::contains(rumble->mStartFrame, from, to)) {
            MR::tryRumblePad(this, rumble->mName, WPAD_CHAN0);
        }
    }
}
#endif

void DemoPadRumbler::update(s32 frame) {
    for (int i = 0; i < mNumPadRumbleEntries; i++) {
        PadRumbleInfo* rumble = &mPadRumbleEntries[i];
        if (rumble->mStartFrame == frame) {
            MR::tryRumblePad(this, rumble->mName, WPAD_CHAN0);
        }
    }
}
