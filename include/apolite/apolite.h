// ApoLite - a lightweight, high-performance system-wide equalizer for Windows 11.
// Copyright (c) 2026 Joel Mandell. MIT licensed (see LICENSE).
#pragma once

#define APOLITE_VERSION_MAJOR 1
#define APOLITE_VERSION_MINOR 0
#define APOLITE_VERSION_PATCH 3
#define APOLITE_VERSION_STRING "1.0.3"

// ApoLite's own APO CLSIDs (independent from Equalizer APO's).
// {8F2C7A14-3B6E-4D51-9C2A-7E5F1D0B4A31}
#define APOLITE_POST_MIX_GUID \
    { 0x8f2c7a14, 0x3b6e, 0x4d51, { 0x9c, 0x2a, 0x7e, 0x5f, 0x1d, 0x0b, 0x4a, 0x31 } }
// {8F2C7A15-3B6E-4D51-9C2A-7E5F1D0B4A31}
#define APOLITE_PRE_MIX_GUID \
    { 0x8f2c7a15, 0x3b6e, 0x4d51, { 0x9c, 0x2a, 0x7e, 0x5f, 0x1d, 0x0b, 0x4a, 0x31 } }

// The same two CLSIDs as registry strings. Windows names an endpoint's APO by
// GUID string, so the installer needs this form; a test checks it matches the
// brace initialisers above.
#define APOLITE_POST_MIX_GUID_STRING L"{8F2C7A14-3B6E-4D51-9C2A-7E5F1D0B4A31}"
#define APOLITE_PRE_MIX_GUID_STRING L"{8F2C7A15-3B6E-4D51-9C2A-7E5F1D0B4A31}"

// Default config location: %ProgramData%\ApoLite\config\config.txt
#define APOLITE_VENDOR_NAME L"ApoLite"
#define APOLITE_FRIENDLY_NAME L"ApoLite"

namespace apolite {

// The APO registration schema (APO_REG_PROPERTIES) carries only a major and a
// minor version, so the patch digit has no field there. It is exposed through
// the binary's VERSIONINFO resource instead, which is what Windows shows in the
// file properties dialog.
constexpr unsigned kVersionMajor = APOLITE_VERSION_MAJOR;
constexpr unsigned kVersionMinor = APOLITE_VERSION_MINOR;
constexpr unsigned kVersionPatch = APOLITE_VERSION_PATCH;

}  // namespace apolite

// GUID constants are only meaningful on Windows; the DSP core and the test
// suite build on any platform without <guiddef.h>.
#if defined(_WIN32) && !defined(APOLITE_NO_GUIDS)

#include <guiddef.h>

namespace apolite {

inline constexpr GUID kPostMixClsid = APOLITE_POST_MIX_GUID;
inline constexpr GUID kPreMixClsid = APOLITE_PRE_MIX_GUID;

}  // namespace apolite

#endif  // _WIN32 && !APOLITE_NO_GUIDS
