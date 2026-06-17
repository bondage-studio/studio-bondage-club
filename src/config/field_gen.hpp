#pragma once

#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

// Declarative JSON field generation. A struct's fields are declared once in a
// SBC_<Type>_FIELDS(X) X-macro list; this header derives to_json, from_json and
// the strict allowed-key set from that single declaration so they can never
// drift. The generated to_json reproduces the historical wire format exactly
// (camelCase keys, ordered output, per-field omit policy), so on-disk config
// files remain byte-compatible.
//
// Each field entry is `X(jsonKey, member, POLICY)` where POLICY is one of the
// EmitPolicy enumerators below (the token pastes directly):
//   ALWAYS     - always emitted.
//   OMIT_EMPTY - emitted only when !value.empty() (strings and vectors).
//   OMIT_ZERO  - emitted only when value != 0 (numeric).
//   OMIT_NULL  - std::optional<T>: emitted (unwrapped) only when it has a value.
// Nested structs and vectors of structs use ALWAYS/OMIT_EMPTY and recurse via
// nlohmann ADL into their own generated (de)serializers.

namespace sbc::config {

enum class EmitPolicy { ALWAYS, OMIT_EMPTY, OMIT_ZERO, OMIT_NULL };

template <class>
struct is_optional : std::false_type {};
template <class T>
struct is_optional<std::optional<T>> : std::true_type {};

template <EmitPolicy P, class T>
inline void emit_field(nlohmann::ordered_json& j, const char* key, const T& v) {
    if constexpr (P == EmitPolicy::OMIT_EMPTY) {
        if (v.empty()) return;
        j[key] = v;
    } else if constexpr (P == EmitPolicy::OMIT_ZERO) {
        if (v == 0) return;
        j[key] = v;
    } else if constexpr (P == EmitPolicy::OMIT_NULL) {
        if (!v.has_value()) return;
        j[key] = *v;
    } else {
        j[key] = v;
    }
}

// read_field merges one key into dst when present and non-null. get_to preserves
// dst's existing value (and, for nested structs, their pre-seeded defaults) when
// the key is absent — matching the historical merge-into-defaults from_json.
template <class T>
inline void read_field(const nlohmann::ordered_json& j, const char* key, T& dst) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return;
    if constexpr (is_optional<T>::value) {
        dst = it->template get<typename T::value_type>();
    } else {
        it->get_to(dst);
    }
}

}  // namespace sbc::config

// --- Per-field expansion macros (one per generated artifact) ---------------
#define SBC_FIELD_EMIT(KEY, MEMBER, POLICY) \
    ::sbc::config::emit_field<::sbc::config::EmitPolicy::POLICY>(j, #KEY, v.MEMBER);
#define SBC_FIELD_READ(KEY, MEMBER, POLICY) ::sbc::config::read_field(j, #KEY, v.MEMBER);
#define SBC_FIELD_KEY(KEY, MEMBER, POLICY) std::string_view{#KEY},

// --- Function-body fragments (used by both the blanket and hand-rolled forms) -
#define SBC_TO_JSON_BODY(Type)                  \
    j = ::nlohmann::ordered_json::object();     \
    SBC_##Type##_FIELDS(SBC_FIELD_EMIT)
#define SBC_FROM_JSON_BODY(Type) SBC_##Type##_FIELDS(SBC_FIELD_READ)

// SBC_DEFINE_STRUCT_JSON defines to_json/from_json for a struct whose every
// field fits the X-list (no conditionally-compiled members). Place inside the
// struct's namespace in a .cpp.
#define SBC_DEFINE_STRUCT_JSON(Type)                              \
    void to_json(::nlohmann::ordered_json& j, const Type& v) {    \
        SBC_TO_JSON_BODY(Type)                                    \
    }                                                             \
    void from_json(const ::nlohmann::ordered_json& j, Type& v) {  \
        SBC_FROM_JSON_BODY(Type)                                  \
    }

// SBC_DEFINE_ALLOWED_KEYS defines `<Type>_keys()` returning the struct's JSON
// keys for strict unknown-field checking. Inline so it lives in a header.
#define SBC_DEFINE_ALLOWED_KEYS(Type)                                            \
    inline std::span<const std::string_view> Type##_keys() {                     \
        static constexpr std::string_view k[] = {SBC_##Type##_FIELDS(SBC_FIELD_KEY)}; \
        return k;                                                                \
    }
