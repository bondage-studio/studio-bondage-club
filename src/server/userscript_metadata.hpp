#pragma once

#include <string>

namespace sbc::server {

// parse_metadata_field extracts the first value of a `// @<field> <value>` line
// inside the `// ==UserScript== ... // ==/UserScript==` metadata block of a
// userscript source. Returns "" when the block or field is absent. This is the
// minimal C++ parse used by the background updater (@version / @downloadURL /
// @updateURL / @name); the frontend has a fuller parser for display.
std::string parse_metadata_field(const std::string& source, const std::string& field);

// version_newer reports whether `candidate` is a strictly newer version string
// than `current`, comparing dot-separated components numerically where both are
// numeric and lexicographically otherwise (Tampermonkey-style). An empty
// candidate is never newer; a non-empty candidate against an empty current is.
bool version_newer(const std::string& candidate, const std::string& current);

}  // namespace sbc::server
