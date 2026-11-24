#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace minicraft::platform::nx {

// Returns the Nintendo Switch system language mapped to a Minicraft+ locale tag
// (e.g. "en-us", "es-es", "es-419", "fr-fr", "ja-jp"), defaulting to "en-us".
std::string DetectSystemLocaleTag();

// Displays the native Switch error LibraryApplet for fatal startup/runtime errors.
// Uses ErrorApplicationConfig in Application mode (10.0.0+ requirement) and falls
// back to ErrorSystemConfig when launched as a regular applet.
void ShowFatalErrorApplet(std::string_view dialog_message,
                          std::string_view details_message = {},
                          uint32_t error_number = 2001);

}  // namespace minicraft::platform::nx
