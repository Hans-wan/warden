// Top-level capability judgment entry point. Pure function, no I/O.
#pragma once
#include <string_view>
#include <vector>

#include "core/capability/tags.h"
#include "core/toolcall/toolcall.h"

namespace core::cap {

// Matches secret-bearing files (.env, id_rsa, .aws/credentials, .ssh/,
// .netrc, .npmrc, .docker/config.json). Case-insensitive substring matching;
// fragments come from the 19-tag taxonomy.
bool isSecretPath(std::string_view path);

// toolcall -> capability tag set. The only function policy calls.
std::vector<Tag> of(const ToolCall& tc);

}  // namespace core::cap
