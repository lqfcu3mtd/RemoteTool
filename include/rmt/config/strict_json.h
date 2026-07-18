#pragma once
// Strict JSON parser for RemoteTool config files (CONFIG_SPEC.md section 1,
// CODING_STANDARDS.md section 9).
//
// Strict mode: rejects comments, trailing commas, duplicate keys, unterminated
// structures, bad escapes, leading zeros, hex/NaN/Infinity numbers, unescaped
// control characters in strings, UTF-8 BOM, and trailing characters after the
// root value. On failure the parser returns an error and never produces a
// partial tree. There is no fallback / lenient mode switch.
//
// String contents are preserved as raw UTF-8 byte sequences; UTF-8 validity is
// not verified byte-by-byte (only the BOM is rejected, per CONFIG_SPEC.md
// section 1).
//
// No third-party dependencies. Hand-written recursive descent.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rmt::config {

enum class JsonType {
    Null,
    Bool,
    Int,
    Double,
    String,
    Array,
    Object,
};

class JsonValue;

using JsonObject = std::vector<std::pair<std::string, JsonValue>>;
using JsonArray = std::vector<JsonValue>;

// Tagged-union JSON value. Default-constructed value is Null. Each instance
// owns its (possibly nested) children via std::string / std::vector.
//
// Type-aware accessors return std::optional<T> / const T* so callers never
// trip an assert on type mismatch; this keeps the API safe for config_schema
// validation code that may probe several shapes.
class JsonValue {
public:
    JsonValue() = default;
    JsonValue(const JsonValue&) = default;
    JsonValue(JsonValue&&) noexcept = default;
    JsonValue& operator=(const JsonValue&) = default;
    JsonValue& operator=(JsonValue&&) noexcept = default;

    JsonType type() const noexcept { return type_; }

    bool is_null() const noexcept { return type_ == JsonType::Null; }
    bool is_bool() const noexcept { return type_ == JsonType::Bool; }
    bool is_int() const noexcept { return type_ == JsonType::Int; }
    bool is_double() const noexcept { return type_ == JsonType::Double; }
    bool is_number() const noexcept {
        return type_ == JsonType::Int || type_ == JsonType::Double;
    }
    bool is_string() const noexcept { return type_ == JsonType::String; }
    bool is_array() const noexcept { return type_ == JsonType::Array; }
    bool is_object() const noexcept { return type_ == JsonType::Object; }

    // Type-aware accessors. Return nullopt / nullptr on type mismatch.
    std::optional<bool> as_bool() const noexcept;
    std::optional<long long> as_int() const noexcept;
    std::optional<double> as_double() const noexcept;
    std::optional<double> as_number() const noexcept;  // int or double -> double
    const std::string* try_as_string() const noexcept;
    const JsonArray* try_as_array() const noexcept;
    const JsonObject* try_as_object() const noexcept;

    // Array access. Returns nullptr if not an array or index out of range.
    const JsonValue* at(std::size_t i) const noexcept;
    // Element count for arrays / objects, 0 for other types.
    std::size_t size() const noexcept;

    // Object access. Returns nullptr if not an object or key not found.
    // Object preserves insertion order (CONFIG_SPEC.md: schema validation can
    // report the first offending field deterministically).
    const JsonValue* find(std::string_view key) const noexcept;
    bool contains(std::string_view key) const noexcept;

    // Factories.
    static JsonValue make_null() noexcept;
    static JsonValue make_bool(bool v) noexcept;
    static JsonValue make_int(long long v) noexcept;
    static JsonValue make_double(double v) noexcept;
    static JsonValue make_string(std::string v);
    static JsonValue make_array(JsonArray v);
    static JsonValue make_object(JsonObject v);

private:
    JsonType type_ = JsonType::Null;
    bool bool_val_ = false;
    long long int_val_ = 0;
    double dbl_val_ = 0.0;
    std::string str_val_;
    JsonArray arr_val_;
    JsonObject obj_val_;
};

struct JsonParseError {
    std::size_t offset = 0;  // byte offset from start of input
    int line = 1;            // 1-based line number
    int column = 1;          // 1-based column number
    std::string reason;
};

// Parse result: either a value tree or an error. We use std::variant directly
// (no generic Result<T,E> template exists in rmt::common yet) so callers can
// use std::get_if / std::holds_alternative or the helpers below.
using JsonParseResult = std::variant<JsonValue, JsonParseError>;

inline bool json_parse_ok(const JsonParseResult& r) noexcept {
    return std::holds_alternative<JsonValue>(r);
}
inline const JsonValue* try_get_json_value(const JsonParseResult& r) noexcept {
    return std::get_if<JsonValue>(&r);
}
inline const JsonParseError* try_get_json_error(const JsonParseResult& r) noexcept {
    return std::get_if<JsonParseError>(&r);
}

// Parse a JSON document. Returns either a JsonValue tree or a JsonParseError
// with location info. Never returns a partial tree on failure.
JsonParseResult parse_json(std::string_view text);

}  // namespace rmt::config
