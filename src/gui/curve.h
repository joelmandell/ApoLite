// ApoLite - response curve drawing for the editor.
// Copyright (c) 2026 Joel Mandell. MIT licensed.
#pragma once

#if defined(_WIN32)
#include <windows.h>

#include "apolite/settings.h"

namespace apolite::gui {

// Draw the combined EQ magnitude of `settings` inside `area`, on a log
// frequency axis from 20 Hz to 20 kHz. The compressor is excluded because its
// effect depends on the input level rather than on frequency alone.
void paintResponseCurve(HDC dc, const RECT& area, const VoiceSettings& settings,
                        double sampleRate);

}  // namespace apolite::gui
#endif
