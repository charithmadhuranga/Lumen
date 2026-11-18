// Lumen — board profile loader.
//
// Parses a board profile JSON document into a BoardProfile. Schema validation
// against schemas/board-profile.schema.json is performed separately (see
// scripts/validate_schemas.mjs / the config schema pipeline); semantic pin
// validation is performed by PinValidator.
#pragma once

#include <string>

#include "board_profile.h"

namespace lumen
{

// Load a board profile from a JSON document. Returns false (and leaves
// `out` unchanged) on parse failure or missing required fields.
bool load_board_profile(const std::string &json, BoardProfile &out);

// Load a board profile from a file path. Returns false if the file cannot be
// read or parsed.
bool load_board_profile_file(const std::string &path, BoardProfile &out);

} // namespace lumen
