#pragma once

#include <filesystem>

namespace engine {

/// Directory containing the running executable.
///
/// Assets are located relative to this rather than the working directory, which
/// differs depending on whether the game is launched from a terminal, from the
/// editor, or by double-clicking it.
const std::filesystem::path& executableDirectory();

} // namespace engine
