#pragma once

// The pre-run Setup screen (design §7).
//
// Two columns: one continuous grouped form covering every conversion option,
// and a dominant viewer showing the live target preview - or, before an image
// is chosen, the history of previous conversions. Returns true when the user
// chose to convert; false ends the session.

struct Configuration;

namespace rc_live_ui {

bool RunSetupScreen(Configuration& cfg, bool show_recent = false);

} // namespace rc_live_ui
