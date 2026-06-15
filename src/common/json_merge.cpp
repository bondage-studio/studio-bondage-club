#include "common/json_merge.hpp"

namespace sbc {

void merge_set_field(nlohmann::json& root, const std::string& key, const nlohmann::json& value) {
    // Fast path: a plain key (no nested path) is assigned literally.
    std::size_t dot = key.find('.');
    if (dot == std::string::npos) {
        root[key] = value;
        return;
    }

    // Walk the dotted path, creating intermediate objects as needed and
    // preserving any existing siblings along the way.
    nlohmann::json* cur = &root;
    std::size_t start = 0;
    while (true) {
        dot = key.find('.', start);
        if (dot == std::string::npos) {
            (*cur)[key.substr(start)] = value;
            return;
        }
        nlohmann::json& next = (*cur)[key.substr(start, dot - start)];
        if (!next.is_object()) next = nlohmann::json::object();
        cur = &next;
        start = dot + 1;
    }
}

void merge_set_fields(nlohmann::json& root, const nlohmann::json& fields) {
    if (!fields.is_object()) return;
    for (auto it = fields.begin(); it != fields.end(); ++it)
        merge_set_field(root, it.key(), it.value());
}

}  // namespace sbc