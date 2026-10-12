#pragma once

#include <QString>

namespace presentation::chat {

// Repository Claude Code works in (empty when the app was not built from a source checkout).
QString sourceDirectory();

// System prompt appended to Claude Code's own for the in-app assistant.
QString assistantInstructions();

} // namespace presentation::chat
