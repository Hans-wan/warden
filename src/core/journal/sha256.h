// Bundled SHA-256 (FIPS 180-4). Zero dependencies, so the distributed binary
// never needs OpenSSL.
#pragma once
#include <string>
#include <string_view>

namespace core::sha256 {
// Returns the 64-character lowercase hex digest.
std::string hex(std::string_view data);
}  // namespace core::sha256
