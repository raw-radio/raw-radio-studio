// raw-radio-studio — Epic 0 bootstrap placeholder.
//
// This is NOT the application. It exists only so the skeleton repository is
// buildable end-to-end with plain CMake. The real JUCE + Tracktion Engine
// application, the audio device I/O, and the tracking UI are implemented by the
// C++ specialist in Epic 0/1.

#include <cstdio>

#ifndef RAW_RADIO_STUDIO_VERSION
#define RAW_RADIO_STUDIO_VERSION "0.0.0-dev"
#endif

int main()
{
    std::printf("raw-radio-studio %s\n", RAW_RADIO_STUDIO_VERSION);
    std::printf("AGPLv3 — Epic 0 bootstrap skeleton (engine not wired yet)\n");
    std::printf("https://github.com/raw-radio/raw-radio-studio\n");
    return 0;
}
