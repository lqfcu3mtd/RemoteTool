#include "rmt/config/strict_json.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>

namespace rmt::config {
namespace {

// Encode a Unicode codepoint as UTF-8 bytes appended to out.
void encode_utf8(std::uint32_t cp, std::string& out) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp & 0x7F));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Hand-written recursive descent parser. Holds a non-owning view over the
// input text plus a (line, column, offset) cursor for error reporting.
class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    JsonParseResult parse() {
        // Reject UTF-8 BOM (CONFIG_SPEC.md section 1).
        if (text_.size() >= 3 &&
            static_cast<unsigned char>(text_[0]) == 0xEF &&
            static_cast<unsigned char>(text_[1]) == 0xBB &&
            static_cast<unsigned char>(text_[2]) == 0xBF) {
            return make_error("UTF-8 BOM is not allowed");
        }

        skip_ws();
        if (at_end()) {
            return make_error("empty input");
        }

        JsonParseResult result = parse_value();
        if (std::holds_alternative<JsonParseError>(result)) {
            return result;
        }

        skip_ws();
        if (!at_end()) {
            return make_error("trailing characters after root value");
        }

        return result;
    }

private:
    std::string_view text_;
    std::size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;

    bool at_end() const noexcept { return pos_ >= text_.size(); }
    char peek() const noexcept { return text_[pos_]; }

    void advance() noexcept {
        if (pos_ < text_.size()) {
            if (text_[pos_] == '\n') {
                ++line_;
                col_ = 1;
            } else {
                ++col_;
            }
            ++pos_;
        }
    }

    JsonParseError make_error(std::string reason) const {
        return JsonParseError{pos_, line_, col_, std::move(reason)};
    }

    void skip_ws() noexcept {
        while (!at_end()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                advance();
            } else {
                break;
            }
        }
    }

    JsonParseResult parse_value() {
        skip_ws();
        if (at_end()) {
            return make_error("unexpected end of input");
        }
        char c = peek();
        switch (c) {
            case '{': return parse_object();
            case '[': return parse_array();
            case '"': return parse_string_value();
            case 't':
            case 'f': return parse_bool_literal();
            case 'n': return parse_null_literal();
            case '-':
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                return parse_number();
            case '/':
                return make_error("comments are not allowed in strict JSON");
            default:
                return make_error(std::string("unexpected character '") + c + "'");
        }
    }

    JsonParseResult parse_object() {
        advance();  // consume '{'
        JsonObject obj;
        std::unordered_set<std::string> seen;

        skip_ws();
        if (!at_end() && peek() == '}') {
            advance();
            return JsonValue::make_object(std::move(obj));
        }

        while (true) {
            skip_ws();
            if (at_end()) {
                return make_error("unterminated object");
            }
            if (peek() != '"') {
                return make_error("expected string key in object");
            }

            std::variant<std::string, JsonParseError> key_result = parse_string_raw();
            if (auto* err = std::get_if<JsonParseError>(&key_result)) {
                return std::move(*err);
            }
            std::string key = std::move(std::get<std::string>(key_result));

            if (!seen.insert(key).second) {
                return make_error("duplicate key: \"" + key + "\"");
            }

            skip_ws();
            if (at_end() || peek() != ':') {
                return make_error("expected ':' after object key");
            }
            advance();  // consume ':'

            JsonParseResult val_result = parse_value();
            if (auto* err = std::get_if<JsonParseError>(&val_result)) {
                return std::move(*err);
            }

            obj.emplace_back(std::move(key), std::move(std::get<JsonValue>(val_result)));

            skip_ws();
            if (at_end()) {
                return make_error("unterminated object");
            }
            char c = peek();
            if (c == ',') {
                advance();
                skip_ws();
                if (!at_end() && peek() == '}') {
                    return make_error("trailing comma in object");
                }
                continue;
            }
            if (c == '}') {
                advance();
                return JsonValue::make_object(std::move(obj));
            }
            return make_error("expected ',' or '}' in object");
        }
    }

    JsonParseResult parse_array() {
        advance();  // consume '['
        JsonArray arr;

        skip_ws();
        if (!at_end() && peek() == ']') {
            advance();
            return JsonValue::make_array(std::move(arr));
        }

        while (true) {
            JsonParseResult val_result = parse_value();
            if (auto* err = std::get_if<JsonParseError>(&val_result)) {
                return std::move(*err);
            }
            arr.push_back(std::move(std::get<JsonValue>(val_result)));

            skip_ws();
            if (at_end()) {
                return make_error("unterminated array");
            }
            char c = peek();
            if (c == ',') {
                advance();
                skip_ws();
                if (!at_end() && peek() == ']') {
                    return make_error("trailing comma in array");
                }
                continue;
            }
            if (c == ']') {
                advance();
                return JsonValue::make_array(std::move(arr));
            }
            return make_error("expected ',' or ']' in array");
        }
    }

    JsonParseResult parse_string_value() {
        std::variant<std::string, JsonParseError> r = parse_string_raw();
        if (auto* err = std::get_if<JsonParseError>(&r)) {
            return std::move(*err);
        }
        return JsonValue::make_string(std::move(std::get<std::string>(r)));
    }

    // Assumes peek() == '"'. Returns the decoded string content (no quotes).
    std::variant<std::string, JsonParseError> parse_string_raw() {
        advance();  // consume opening '"'
        std::string out;

        while (true) {
            if (at_end()) {
                return make_error("unterminated string");
            }
            char c = peek();
            unsigned char uc = static_cast<unsigned char>(c);
            if (uc < 0x20) {
                return make_error("unescaped control character in string");
            }
            if (c == '"') {
                advance();
                return out;
            }
            if (c == '\\') {
                advance();
                if (at_end()) {
                    return make_error("unterminated escape sequence");
                }
                char esc = peek();
                switch (esc) {
                    case '"': out.push_back('"'); advance(); break;
                    case '\\': out.push_back('\\'); advance(); break;
                    case '/': out.push_back('/'); advance(); break;
                    case 'b': out.push_back('\b'); advance(); break;
                    case 'f': out.push_back('\f'); advance(); break;
                    case 'n': out.push_back('\n'); advance(); break;
                    case 'r': out.push_back('\r'); advance(); break;
                    case 't': out.push_back('\t'); advance(); break;
                    case 'u': {
                        advance();  // consume 'u'
                        std::variant<std::uint32_t, JsonParseError> cp_result =
                            parse_unicode_escape();
                        if (auto* err = std::get_if<JsonParseError>(&cp_result)) {
                            return std::move(*err);
                        }
                        encode_utf8(std::get<std::uint32_t>(cp_result), out);
                        break;
                    }
                    default:
                        return make_error(std::string("invalid escape sequence \\") + esc);
                }
            } else {
                out.push_back(c);
                advance();
            }
        }
    }

    std::variant<std::uint32_t, JsonParseError> parse_hex4() {
        if (pos_ + 4 > text_.size()) {
            return make_error("incomplete \\u escape (expected 4 hex digits)");
        }
        std::uint32_t cp = 0;
        for (int i = 0; i < 4; ++i) {
            char c = peek();
            std::uint32_t digit;
            if (c >= '0' && c <= '9') {
                digit = static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                digit = static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                return make_error("invalid hex digit in \\u escape");
            }
            cp = (cp << 4) | digit;
            advance();
        }
        return cp;
    }

    // 'u' has already been consumed. Parses 4 hex digits and, if the result is
    // a high surrogate, requires a following \uLOW surrogate and combines them
    // into the codepoint.
    std::variant<std::uint32_t, JsonParseError> parse_unicode_escape() {
        std::variant<std::uint32_t, JsonParseError> cp1_result = parse_hex4();
        if (auto* err = std::get_if<JsonParseError>(&cp1_result)) {
            return std::move(*err);
        }
        std::uint32_t cp1 = std::get<std::uint32_t>(cp1_result);

        if (cp1 >= 0xD800 && cp1 <= 0xDBFF) {
            // High surrogate; require a following \uXXXX low surrogate.
            if (pos_ + 1 >= text_.size() ||
                text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
                return make_error("expected low surrogate \\uXXXX after high surrogate");
            }
            advance();  // '\'
            advance();  // 'u'
            std::variant<std::uint32_t, JsonParseError> cp2_result = parse_hex4();
            if (auto* err = std::get_if<JsonParseError>(&cp2_result)) {
                return std::move(*err);
            }
            std::uint32_t cp2 = std::get<std::uint32_t>(cp2_result);
            if (cp2 < 0xDC00 || cp2 > 0xDFFF) {
                return make_error("invalid low surrogate after high surrogate");
            }
            std::uint32_t combined = 0x10000 + ((cp1 - 0xD800) << 10) + (cp2 - 0xDC00);
            return combined;
        }

        if (cp1 >= 0xDC00 && cp1 <= 0xDFFF) {
            return make_error("unexpected low surrogate without preceding high surrogate");
        }

        return cp1;
    }

    JsonParseResult parse_bool_literal() {
        if (text_.compare(pos_, 4, "true") == 0) {
            for (int i = 0; i < 4; ++i) advance();
            return JsonValue::make_bool(true);
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            for (int i = 0; i < 5; ++i) advance();
            return JsonValue::make_bool(false);
        }
        return make_error("invalid literal (expected true or false)");
    }

    JsonParseResult parse_null_literal() {
        if (text_.compare(pos_, 4, "null") == 0) {
            for (int i = 0; i < 4; ++i) advance();
            return JsonValue::make_null();
        }
        return make_error("invalid literal (expected null)");
    }

    JsonParseResult parse_number() {
        const std::size_t start = pos_;

        if (!at_end() && peek() == '-') {
            advance();
            if (at_end()) {
                return make_error("expected digit after '-'");
            }
        }

        char first = peek();
        if (first == '0') {
            advance();
            if (!at_end()) {
                char next = peek();
                if (next >= '0' && next <= '9') {
                    return make_error("leading zero not allowed in number");
                }
                if (next == 'x' || next == 'X') {
                    return make_error("hexadecimal numbers not allowed");
                }
            }
        } else if (first >= '1' && first <= '9') {
            advance();
            while (!at_end() && peek() >= '0' && peek() <= '9') {
                advance();
            }
        } else {
            return make_error("invalid number");
        }

        bool is_double = false;

        // Fractional part
        if (!at_end() && peek() == '.') {
            is_double = true;
            advance();
            if (at_end() || peek() < '0' || peek() > '9') {
                return make_error("expected digit after '.' in number");
            }
            while (!at_end() && peek() >= '0' && peek() <= '9') {
                advance();
            }
        }

        // Exponent part
        if (!at_end() && (peek() == 'e' || peek() == 'E')) {
            is_double = true;
            advance();
            if (!at_end() && (peek() == '+' || peek() == '-')) {
                advance();
            }
            if (at_end() || peek() < '0' || peek() > '9') {
                return make_error("expected digit in exponent");
            }
            while (!at_end() && peek() >= '0' && peek() <= '9') {
                advance();
            }
        }

        std::string num_str(text_.substr(start, pos_ - start));

        if (is_double) {
            errno = 0;
            char* end = nullptr;
            double d = std::strtod(num_str.c_str(), &end);
            if (errno != 0 || end == num_str.c_str() || *end != '\0') {
                return make_error("invalid number: " + num_str);
            }
            return JsonValue::make_double(d);
        }

        errno = 0;
        char* end = nullptr;
        long long i = std::strtoll(num_str.c_str(), &end, 10);
        if (errno != 0 || end == num_str.c_str() || *end != '\0') {
            return make_error("invalid integer: " + num_str);
        }
        return JsonValue::make_int(i);
    }
};

}  // namespace

// --- JsonValue accessors ---

std::optional<bool> JsonValue::as_bool() const noexcept {
    return type_ == JsonType::Bool ? std::optional<bool>(bool_val_) : std::nullopt;
}
std::optional<long long> JsonValue::as_int() const noexcept {
    return type_ == JsonType::Int ? std::optional<long long>(int_val_) : std::nullopt;
}
std::optional<double> JsonValue::as_double() const noexcept {
    return type_ == JsonType::Double ? std::optional<double>(dbl_val_) : std::nullopt;
}
std::optional<double> JsonValue::as_number() const noexcept {
    if (type_ == JsonType::Int) return static_cast<double>(int_val_);
    if (type_ == JsonType::Double) return dbl_val_;
    return std::nullopt;
}
const std::string* JsonValue::try_as_string() const noexcept {
    return type_ == JsonType::String ? &str_val_ : nullptr;
}
const JsonArray* JsonValue::try_as_array() const noexcept {
    return type_ == JsonType::Array ? &arr_val_ : nullptr;
}
const JsonObject* JsonValue::try_as_object() const noexcept {
    return type_ == JsonType::Object ? &obj_val_ : nullptr;
}

const JsonValue* JsonValue::at(std::size_t i) const noexcept {
    if (type_ != JsonType::Array || i >= arr_val_.size()) return nullptr;
    return &arr_val_[i];
}

std::size_t JsonValue::size() const noexcept {
    if (type_ == JsonType::Array) return arr_val_.size();
    if (type_ == JsonType::Object) return obj_val_.size();
    return 0;
}

const JsonValue* JsonValue::find(std::string_view key) const noexcept {
    if (type_ != JsonType::Object) return nullptr;
    for (const auto& kv : obj_val_) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

bool JsonValue::contains(std::string_view key) const noexcept {
    return find(key) != nullptr;
}

// --- JsonValue factories ---

JsonValue JsonValue::make_null() noexcept {
    JsonValue v;
    v.type_ = JsonType::Null;
    return v;
}
JsonValue JsonValue::make_bool(bool b) noexcept {
    JsonValue v;
    v.type_ = JsonType::Bool;
    v.bool_val_ = b;
    return v;
}
JsonValue JsonValue::make_int(long long i) noexcept {
    JsonValue v;
    v.type_ = JsonType::Int;
    v.int_val_ = i;
    return v;
}
JsonValue JsonValue::make_double(double d) noexcept {
    JsonValue v;
    v.type_ = JsonType::Double;
    v.dbl_val_ = d;
    return v;
}
JsonValue JsonValue::make_string(std::string s) {
    JsonValue v;
    v.type_ = JsonType::String;
    v.str_val_ = std::move(s);
    return v;
}
JsonValue JsonValue::make_array(JsonArray v) {
    JsonValue r;
    r.type_ = JsonType::Array;
    r.arr_val_ = std::move(v);
    return r;
}
JsonValue JsonValue::make_object(JsonObject v) {
    JsonValue r;
    r.type_ = JsonType::Object;
    r.obj_val_ = std::move(v);
    return r;
}

// --- Public entry point ---

JsonParseResult parse_json(std::string_view text) {
    Parser p(text);
    return p.parse();
}

}  // namespace rmt::config
