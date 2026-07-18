// Strict JSON parser tests (CONFIG_SPEC.md section 1, CODING_STANDARDS.md
// section 9). Pure C++17, no sockets, no Windows APIs -> compiles under MinGW
// or MSVC.
#include <string>
#include <string_view>

#include "rmt/config/strict_json.h"
#include "rmt/test.h"

using namespace rmt::config;

namespace {

void run_legal_tests() {
    // Empty object / array
    {
        auto r = parse_json("{}");
        RMT_CHECK_MSG(json_parse_ok(r), "empty object should parse");
        auto* v = try_get_json_value(r);
        RMT_CHECK(v != nullptr);
        RMT_CHECK(v->is_object());
        RMT_CHECK(v->size() == 0);
    }
    {
        auto r = parse_json("[]");
        RMT_CHECK_MSG(json_parse_ok(r), "empty array should parse");
        auto* v = try_get_json_value(r);
        RMT_CHECK(v->is_array());
        RMT_CHECK(v->size() == 0);
    }

    // Literals
    {
        auto r = parse_json("null");
        RMT_CHECK(json_parse_ok(r));
        RMT_CHECK(try_get_json_value(r)->is_null());
    }
    {
        auto r = parse_json("true");
        auto* v = try_get_json_value(r);
        RMT_CHECK(v->is_bool());
        RMT_CHECK(v->as_bool() == true);
    }
    {
        auto r = parse_json("false");
        auto* v = try_get_json_value(r);
        RMT_CHECK(v->is_bool());
        RMT_CHECK(v->as_bool() == false);
    }

    // Numbers (integers and floats)
    {
        auto r = parse_json("0");
        auto* v = try_get_json_value(r);
        RMT_CHECK(v->is_int());
        RMT_CHECK(v->as_int() == 0LL);
    }
    {
        auto r = parse_json("-1");
        auto* v = try_get_json_value(r);
        RMT_CHECK(v->is_int());
        RMT_CHECK(v->as_int() == -1LL);
    }
    {
        auto r = parse_json("100");
        auto* v = try_get_json_value(r);
        RMT_CHECK(v->is_int());
        RMT_CHECK(v->as_int() == 100LL);
    }
    {
        auto r = parse_json("3.14");
        auto* v = try_get_json_value(r);
        RMT_CHECK(v->is_double());
        RMT_CHECK(v->as_double() == 3.14);
    }
    {
        auto r = parse_json("1e10");
        auto* v = try_get_json_value(r);
        RMT_CHECK(v->is_double());
        RMT_CHECK(v->as_double() == 1e10);
    }
    {
        auto r = parse_json("-0.5");
        auto* v = try_get_json_value(r);
        RMT_CHECK(v->is_double());
        RMT_CHECK(v->as_double() == -0.5);
    }
    {
        auto r = parse_json("1.5e-3");
        auto* v = try_get_json_value(r);
        RMT_CHECK(v->is_double());
        RMT_CHECK(v->as_double() == 1.5e-3);
    }
    {
        auto r = parse_json("1E10");
        auto* v = try_get_json_value(r);
        RMT_CHECK(v->is_double());
        RMT_CHECK(v->as_double() == 1e10);
    }
    {
        auto r = parse_json("0.0");
        auto* v = try_get_json_value(r);
        RMT_CHECK(v->is_double());
        RMT_CHECK(v->as_double() == 0.0);
    }
    {
        auto r = parse_json("-0");
        auto* v = try_get_json_value(r);
        RMT_CHECK(v->is_int());
        RMT_CHECK(v->as_int() == 0LL);
    }

    // Strings with escapes
    {
        auto r = parse_json("\"abc\"");
        auto* v = try_get_json_value(r);
        RMT_CHECK(v->is_string());
        RMT_CHECK(*v->try_as_string() == "abc");
    }
    {
        auto r = parse_json("\"\\n\\t\\r\"");
        auto* v = try_get_json_value(r);
        RMT_CHECK(*v->try_as_string() == "\n\t\r");
    }
    {
        auto r = parse_json("\"\\u0041\"");
        auto* v = try_get_json_value(r);
        RMT_CHECK(*v->try_as_string() == "A");
    }
    {
        // surrogate pair U+10000 -> UTF-8 F0 90 80 80
        auto r = parse_json("\"\\uD800\\uDC00\"");
        auto* v = try_get_json_value(r);
        RMT_CHECK(*v->try_as_string() == std::string("\xF0\x90\x80\x80"));
    }
    {
        auto r = parse_json("\"a\\\\b\\\"c\"");
        auto* v = try_get_json_value(r);
        RMT_CHECK(*v->try_as_string() == "a\\b\"c");
    }
    {
        auto r = parse_json("\"a\\/b\"");
        auto* v = try_get_json_value(r);
        RMT_CHECK(*v->try_as_string() == "a/b");
    }
    {
        auto r = parse_json("\"\\b\\f\"");
        auto* v = try_get_json_value(r);
        RMT_CHECK(*v->try_as_string() == "\b\f");
    }
    {
        // UTF-8 multibyte preserved as raw bytes
        auto r = parse_json("\"\xE4\xB8\xAD\xE6\x96\x87\"");
        auto* v = try_get_json_value(r);
        RMT_CHECK(*v->try_as_string() == "\xE4\xB8\xAD\xE6\x96\x87");
    }

    // Nested structures
    {
        auto r = parse_json("{\"a\": [1, 2, {\"b\": \"c\"}]}");
        auto* v = try_get_json_value(r);
        RMT_CHECK(v->is_object());
        auto* arr = v->find("a");
        RMT_CHECK(arr != nullptr);
        RMT_CHECK(arr->is_array());
        RMT_CHECK(arr->size() == 3);
        RMT_CHECK(arr->at(0)->as_int() == 1LL);
        RMT_CHECK(arr->at(1)->as_int() == 2LL);
        auto* inner = arr->at(2);
        RMT_CHECK(inner->is_object());
        auto* b = inner->find("b");
        RMT_CHECK(b != nullptr);
        RMT_CHECK(*b->try_as_string() == "c");
    }

    // Config-like sample (CONFIG_SPEC.md section 3)
    {
        std::string text = R"({
            "schema_version": 1,
            "agent_listener": {
                "bind_host": "0.0.0.0",
                "port": 4433
            },
            "limits": {
                "max_sessions_per_mapping": 32
            }
        })";
        auto r = parse_json(text);
        RMT_CHECK_MSG(json_parse_ok(r), "config sample should parse");
        auto* v = try_get_json_value(r);
        RMT_CHECK(v->is_object());
        RMT_CHECK(v->contains("schema_version"));
        RMT_CHECK(v->find("schema_version")->as_int() == 1LL);
        RMT_CHECK(v->find("agent_listener")->is_object());
        RMT_CHECK(v->find("agent_listener")->find("port")->as_int() == 4433LL);
    }

    // Whitespace handling
    {
        auto r = parse_json("  {  }  ");
        RMT_CHECK(json_parse_ok(r));
    }
    {
        auto r = parse_json("\n\t[1,\n2]\n");
        RMT_CHECK(json_parse_ok(r));
    }

    // Insertion order preserved (object iteration)
    {
        auto r = parse_json("{\"z\": 1, \"a\": 2, \"m\": 3}");
        auto* v = try_get_json_value(r);
        const JsonObject* obj = v->try_as_object();
        RMT_CHECK(obj != nullptr);
        RMT_CHECK(obj->size() == 3);
        RMT_CHECK(obj->at(0).first == "z");
        RMT_CHECK(obj->at(1).first == "a");
        RMT_CHECK(obj->at(2).first == "m");
    }
}

void run_reject_tests() {
    // Comments
    RMT_CHECK_MSG(!json_parse_ok(parse_json("// comment")), "reject line comment");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("/* comment */")), "reject block comment");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("[1, // foo\n2]")), "reject inline comment");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("{\"a\":1/*x*/}")), "reject block comment in object");

    // Trailing commas
    RMT_CHECK_MSG(!json_parse_ok(parse_json("[1, 2,]")), "reject trailing comma in array");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("[1,]")), "reject single trailing comma");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("{\"a\": 1,}")), "reject trailing comma in object");

    // Duplicate keys
    RMT_CHECK_MSG(!json_parse_ok(parse_json("{\"a\": 1, \"a\": 2}")), "reject duplicate keys");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("{\"a\": 1, \"a\": 2, \"a\": 3}")), "reject triple duplicate");

    // Unterminated structures
    RMT_CHECK_MSG(!json_parse_ok(parse_json("{\"a\": 1")), "reject unterminated object");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("[1, 2")), "reject unterminated array");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("\"abc")), "reject unterminated string");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("{")), "reject lone open brace");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("[")), "reject lone open bracket");

    // Bad escapes
    RMT_CHECK_MSG(!json_parse_ok(parse_json("\"\\q\"")), "reject bad escape \\q");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("\"\\x\"")), "reject bad escape \\x");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("\"\\u00\"")), "reject incomplete \\u escape");

    // NaN / Infinity
    RMT_CHECK_MSG(!json_parse_ok(parse_json("NaN")), "reject NaN");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("Infinity")), "reject Infinity");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("-NaN")), "reject -NaN");

    // Leading zero / hex / leading plus
    RMT_CHECK_MSG(!json_parse_ok(parse_json("0123")), "reject leading zero");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("0x1")), "reject hex literal");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("+1")), "reject leading plus");

    // UTF-8 BOM (CONFIG_SPEC.md section 1)
    {
        std::string bom;
        bom.push_back(static_cast<char>(0xEF));
        bom.push_back(static_cast<char>(0xBB));
        bom.push_back(static_cast<char>(0xBF));
        bom += "{}";
        RMT_CHECK_MSG(!json_parse_ok(parse_json(bom)), "reject UTF-8 BOM");
    }

    // Trailing garbage
    RMT_CHECK_MSG(!json_parse_ok(parse_json("{}x")), "reject trailing garbage");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("null x")), "reject trailing garbage after null");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("true false")), "reject trailing garbage after bool");

    // Empty / whitespace-only input
    RMT_CHECK_MSG(!json_parse_ok(parse_json("")), "reject empty input");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("   ")), "reject whitespace-only input");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("\n\n")), "reject newline-only input");

    // Unescaped control characters in strings
    {
        std::string s = "\"abc";
        s.push_back(static_cast<char>(0x01));
        s.push_back('"');
        RMT_CHECK_MSG(!json_parse_ok(parse_json(s)), "reject unescaped control char");
    }
    {
        std::string s = "\"";
        s.push_back(static_cast<char>(0x00));
        s.push_back('"');
        RMT_CHECK_MSG(!json_parse_ok(parse_json(s)), "reject NUL in string");
    }

    // Bad literals
    RMT_CHECK_MSG(!json_parse_ok(parse_json("tru")), "reject bad literal tru");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("nul")), "reject bad literal nul");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("True")), "reject capital T");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("NULL")), "reject capital N");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("truex")), "reject literal followed by garbage");

    // Invalid numbers
    RMT_CHECK_MSG(!json_parse_ok(parse_json("1.")), "reject trailing dot");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("1e")), "reject trailing e");
    RMT_CHECK_MSG(!json_parse_ok(parse_json(".")), "reject lone dot");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("-")), "reject lone minus");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("1.2.3")), "reject double dot");

    // Bad object / array syntax
    RMT_CHECK_MSG(!json_parse_ok(parse_json("{1: 2}")), "reject non-string key");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("{\"a\" 1}")), "reject missing colon");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("{\"a\": 1 2}")), "reject missing comma in object");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("[1 2]")), "reject missing comma in array");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("}")), "reject lone close brace");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("]")), "reject lone close bracket");
    RMT_CHECK_MSG(!json_parse_ok(parse_json(":")), "reject lone colon");
    RMT_CHECK_MSG(!json_parse_ok(parse_json(",")), "reject lone comma");

    // Bad surrogate pairs
    RMT_CHECK_MSG(!json_parse_ok(parse_json("\"\\uD800\"")), "reject high surrogate alone");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("\"\\uDC00\"")), "reject low surrogate alone");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("\"\\uD800\\u0041\"")), "reject high surrogate followed by non-low");
    RMT_CHECK_MSG(!json_parse_ok(parse_json("\"\\uD800abcd\"")), "reject high surrogate not followed by \\u");
}

void run_error_position_tests() {
    // Error carries location info
    {
        auto r = parse_json("{\n  \"a\": 1,\n  \"a\": 2\n}");
        auto* e = try_get_json_error(r);
        RMT_CHECK(e != nullptr);
        RMT_CHECK(e->line >= 1);
        RMT_CHECK(e->column >= 1);
        RMT_CHECK(!e->reason.empty());
        RMT_CHECK(e->reason.find("duplicate") != std::string::npos);
    }
    // Error on line 3
    {
        auto r = parse_json("{\n  \"a\": 1,\n  invalid\n}");
        auto* e = try_get_json_error(r);
        RMT_CHECK(e != nullptr);
        RMT_CHECK(e->line == 3);
    }
    // BOM error reported at offset 0
    {
        std::string bom;
        bom.push_back(static_cast<char>(0xEF));
        bom.push_back(static_cast<char>(0xBB));
        bom.push_back(static_cast<char>(0xBF));
        bom += "{}";
        auto r = parse_json(bom);
        auto* e = try_get_json_error(r);
        RMT_CHECK(e != nullptr);
        RMT_CHECK(e->offset == 0);
        RMT_CHECK(e->line == 1);
        RMT_CHECK(e->column == 1);
    }
    // Empty input reports offset 0
    {
        auto r = parse_json("");
        auto* e = try_get_json_error(r);
        RMT_CHECK(e != nullptr);
        RMT_CHECK(e->offset == 0);
        RMT_CHECK(e->line == 1);
    }
}

void run_api_tests() {
    // Build a complex tree via factories
    JsonValue obj = JsonValue::make_object({
        {"name", JsonValue::make_string("RemoteTool")},
        {"port", JsonValue::make_int(4433)},
        {"enabled", JsonValue::make_bool(true)},
        {"nested", JsonValue::make_object({
            {"k", JsonValue::make_null()}
        })},
        {"arr", JsonValue::make_array({
            JsonValue::make_int(1),
            JsonValue::make_int(2),
            JsonValue::make_string("three")
        })}
    });

    RMT_CHECK(obj.is_object());
    RMT_CHECK(obj.size() == 5);
    RMT_CHECK(obj.contains("name"));
    RMT_CHECK(!obj.contains("missing"));
    RMT_CHECK(obj.find("name")->is_string());
    RMT_CHECK(*obj.find("name")->try_as_string() == "RemoteTool");
    RMT_CHECK(obj.find("port")->as_int() == 4433LL);
    RMT_CHECK(obj.find("enabled")->as_bool() == true);
    RMT_CHECK(obj.find("nested")->is_object());
    RMT_CHECK(obj.find("nested")->find("k")->is_null());
    RMT_CHECK(obj.find("arr")->is_array());
    RMT_CHECK(obj.find("arr")->size() == 3);
    RMT_CHECK(obj.find("arr")->at(0)->as_int() == 1LL);
    RMT_CHECK(obj.find("arr")->at(2)->is_string());
    RMT_CHECK(obj.find("arr")->at(99) == nullptr);
    RMT_CHECK(obj.find("missing") == nullptr);

    // Type mismatch returns nullopt / nullptr
    RMT_CHECK(!obj.as_int().has_value());
    RMT_CHECK(obj.try_as_array() == nullptr);
    RMT_CHECK(obj.try_as_string() == nullptr);
    RMT_CHECK(obj.as_bool() == std::nullopt);

    // Default-constructed value is Null
    JsonValue def;
    RMT_CHECK(def.is_null());
    RMT_CHECK(def.type() == JsonType::Null);

    // Copy semantics
    JsonValue copy = obj;
    RMT_CHECK(copy.is_object());
    RMT_CHECK(copy.size() == 5);
    RMT_CHECK(copy.find("name")->is_string());

    // Move semantics
    JsonValue moved = std::move(copy);
    RMT_CHECK(moved.is_object());
    RMT_CHECK(moved.size() == 5);

    // Non-container types return nullptr / 0 from container accessors
    JsonValue num = JsonValue::make_int(42);
    RMT_CHECK(num.find("x") == nullptr);
    RMT_CHECK(num.at(0) == nullptr);
    RMT_CHECK(num.size() == 0);
    RMT_CHECK(num.contains("x") == false);

    // as_number convenience: int and double both return a double
    JsonValue i = JsonValue::make_int(7);
    JsonValue d = JsonValue::make_double(2.5);
    RMT_CHECK(i.as_number() == 7.0);
    RMT_CHECK(d.as_number() == 2.5);
    // is_int is not is_double
    RMT_CHECK(!i.as_double().has_value());
    RMT_CHECK(!d.as_int().has_value());

    // Round-trip via parse produces equivalent tree
    {
        auto r = parse_json("{\"x\": [1, 2, 3]}");
        auto* v = try_get_json_value(r);
        RMT_CHECK(v != nullptr);
        const JsonArray* a = v->find("x")->try_as_array();
        RMT_CHECK(a != nullptr);
        RMT_CHECK(a->size() == 3);
        RMT_CHECK((*a)[0].as_int() == 1LL);
        RMT_CHECK((*a)[1].as_int() == 2LL);
        RMT_CHECK((*a)[2].as_int() == 3LL);
    }
}

}  // namespace

int main() {
    run_legal_tests();
    run_reject_tests();
    run_error_position_tests();
    run_api_tests();
    auto& c = rmt::test::ctx();
    std::printf("strict_json_test: %d passed, %d failed\n", c.passed, c.failed);
    return c.ok() ? 0 : 1;
}
