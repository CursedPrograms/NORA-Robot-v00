// Minimal recursive-descent JSON reader.
//
// NORA's endpoints only ever return small, flat-ish objects (numbers,
// strings, one level of array-of-objects for /robots), so a full JSON
// library would be a dependency this project doesn't need. This covers
// null/bool/number/string/array/object, which is everything the ESP32
// side ever emits.
#pragma once

#include <cctype>
#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    JsonValue() : type_(Type::Null) {}

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }

    double as_number(double fallback = 0.0) const {
        return type_ == Type::Number ? number_ : fallback;
    }
    int as_int(int fallback = 0) const {
        return type_ == Type::Number ? static_cast<int>(number_) : fallback;
    }
    bool as_bool(bool fallback = false) const {
        return type_ == Type::Bool ? bool_ : fallback;
    }
    const std::string& as_string(const std::string& fallback = "") const {
        return type_ == Type::String ? string_ : fallback;
    }

    bool contains(const std::string& key) const {
        return type_ == Type::Object && object_.count(key) > 0;
    }

    // Object access -- returns a shared Null instance if the key is absent,
    // so chained lookups like j["a"]["b"].as_int() never throw.
    const JsonValue& operator[](const std::string& key) const {
        static const JsonValue null_value;
        if (type_ != Type::Object) return null_value;
        auto it = object_.find(key);
        return it == object_.end() ? null_value : it->second;
    }

    size_t size() const { return type_ == Type::Array ? array_.size() : 0; }

    const JsonValue& operator[](size_t idx) const {
        static const JsonValue null_value;
        if (type_ != Type::Array || idx >= array_.size()) return null_value;
        return array_[idx];
    }

    static JsonValue parse(const std::string& text) {
        size_t pos = 0;
        JsonValue v = parse_value(text, pos);
        return v;
    }

private:
    Type type_;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<JsonValue> array_;
    std::map<std::string, JsonValue> object_;

    static void skip_ws(const std::string& s, size_t& i) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
    }

    static JsonValue parse_value(const std::string& s, size_t& i) {
        skip_ws(s, i);
        if (i >= s.size()) return JsonValue();
        char c = s[i];
        if (c == '{') return parse_object(s, i);
        if (c == '[') return parse_array(s, i);
        if (c == '"') return parse_string_value(s, i);
        if (c == 't' || c == 'f') return parse_bool(s, i);
        if (c == 'n') { i += 4; return JsonValue(); }  // "null"
        return parse_number(s, i);
    }

    static JsonValue parse_object(const std::string& s, size_t& i) {
        JsonValue v;
        v.type_ = Type::Object;
        i++;  // '{'
        skip_ws(s, i);
        if (i < s.size() && s[i] == '}') { i++; return v; }
        while (i < s.size()) {
            skip_ws(s, i);
            std::string key = parse_raw_string(s, i);
            skip_ws(s, i);
            if (i < s.size() && s[i] == ':') i++;
            JsonValue val = parse_value(s, i);
            v.object_[key] = std::move(val);
            skip_ws(s, i);
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == '}') { i++; break; }
            break;
        }
        return v;
    }

    static JsonValue parse_array(const std::string& s, size_t& i) {
        JsonValue v;
        v.type_ = Type::Array;
        i++;  // '['
        skip_ws(s, i);
        if (i < s.size() && s[i] == ']') { i++; return v; }
        while (i < s.size()) {
            JsonValue val = parse_value(s, i);
            v.array_.push_back(std::move(val));
            skip_ws(s, i);
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == ']') { i++; break; }
            break;
        }
        return v;
    }

    static std::string parse_raw_string(const std::string& s, size_t& i) {
        std::string out;
        if (i >= s.size() || s[i] != '"') return out;
        i++;  // opening quote
        while (i < s.size() && s[i] != '"') {
            char c = s[i];
            if (c == '\\' && i + 1 < s.size()) {
                char esc = s[i + 1];
                switch (esc) {
                    case '"':  out += '"';  i += 2; break;
                    case '\\': out += '\\'; i += 2; break;
                    case '/':  out += '/';  i += 2; break;
                    case 'b':  out += '\b'; i += 2; break;
                    case 'f':  out += '\f'; i += 2; break;
                    case 'n':  out += '\n'; i += 2; break;
                    case 'r':  out += '\r'; i += 2; break;
                    case 't':  out += '\t'; i += 2; break;
                    case 'u':
                        // Minimal \uXXXX handling: NORA's payloads are ASCII
                        // (names, types, ip strings), so a single-byte
                        // truncation is enough -- no surrogate pairs expected.
                        if (i + 5 < s.size()) {
                            unsigned code = static_cast<unsigned>(
                                std::strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16));
                            out += static_cast<char>(code & 0xFF);
                            i += 6;
                        } else {
                            i += 2;
                        }
                        break;
                    default:
                        out += esc; i += 2; break;
                }
            } else {
                out += c;
                i++;
            }
        }
        if (i < s.size()) i++;  // closing quote
        return out;
    }

    static JsonValue parse_string_value(const std::string& s, size_t& i) {
        JsonValue v;
        v.type_ = Type::String;
        v.string_ = parse_raw_string(s, i);
        return v;
    }

    static JsonValue parse_bool(const std::string& s, size_t& i) {
        JsonValue v;
        v.type_ = Type::Bool;
        if (s.compare(i, 4, "true") == 0) { v.bool_ = true; i += 4; }
        else if (s.compare(i, 5, "false") == 0) { v.bool_ = false; i += 5; }
        return v;
    }

    static JsonValue parse_number(const std::string& s, size_t& i) {
        JsonValue v;
        v.type_ = Type::Number;
        size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;
        while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) ||
                                 s[i] == '.' || s[i] == 'e' || s[i] == 'E' ||
                                 s[i] == '+' || s[i] == '-')) i++;
        if (i > start) {
            v.number_ = std::strtod(s.substr(start, i - start).c_str(), nullptr);
        }
        return v;
    }
};
