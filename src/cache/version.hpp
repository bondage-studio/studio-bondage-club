#pragma once

#include <string>

namespace sbc {
class Url;
}

namespace sbc::cache {

// extract_version returns a version label from a request's target URL per the
// rule's `version` spec. Supported specs:
//   "query:<name>"  -> value of query parameter <name> (e.g. echo GitHub Pages ?v=)
//   "re:<regexp>"   -> first capture group (or whole match) of <regexp> applied
//                      to the full URL string (e.g. game "/R129/" or jsDelivr @sha)
// An empty spec or no match returns "". Throws sbc::Error on an invalid regexp
// (callers may validate specs up front).
std::string extract_version(const std::string& spec, const Url& target);

// rewrite_key_path rewrites an upstream-relative path into a canonical cache-key
// string using a "re:<regexp>" pattern and a std::regex_replace template ($1
// references). This collapses different URL shapes of the same resource onto one
// key (e.g. Echo's GitHub-Pages and jsDelivr paths) and strips embedded version
// tokens. An empty pattern returns real_path unchanged.
std::string rewrite_key_path(const std::string& real_path, const std::string& key_pattern,
                             const std::string& key_template);

}  // namespace sbc::cache
