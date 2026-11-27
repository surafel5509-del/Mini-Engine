#pragma once
// Minimal dependency-free JSON parser shared by the scene serializer
// and the glTF loader. Supports objects, arrays, strings, numbers,
// booleans and null — everything the engine's file formats need.
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace mini::json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string str;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    const Value* find(const std::string& key) const {
        if (type != Type::Object) return nullptr;
        for (auto& kv : object) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    bool isObject() const { return type == Type::Object; }
    bool isArray() const { return type == Type::Array; }
    bool isString() const { return type == Type::String; }
    bool isNumber() const { return type == Type::Number; }
};

class Parser {
public:
    explicit Parser(const std::string& text) : mText(text) {}

    bool parse(Value& out) {
        try {
            skipWs();
            if (!parseValue(out)) return false;
            skipWs();
            return mPos >= mText.size();
        } catch (...) {
            return false;
        }
    }

private:
    void skipWs() {
        while (mPos < mText.size() &&
               std::isspace(static_cast<unsigned char>(mText[mPos]))) ++mPos;
    }
    bool consume(char c) {
        skipWs();
        if (mPos < mText.size() && mText[mPos] == c) { ++mPos; return true; }
        return false;
    }
    bool parseValue(Value& out) {
        skipWs();
        if (mPos >= mText.size()) return false;
        char c = mText[mPos];
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') { out.type = Value::Type::String; return parseString(out.str); }
        if (c == 't' || c == 'f') return parseBool(out);
        if (c == 'n' && mText.compare(mPos, 4, "null") == 0) {
            mPos += 4; out.type = Value::Type::Null; return true;
        }
        return parseNumber(out);
    }
    bool parseObject(Value& out) {
        out.type = Value::Type::Object;
        ++mPos;
        skipWs();
        if (consume('}')) return true;
        for (;;) {
            std::string key;
            if (!parseString(key)) return false;
            if (!consume(':')) return false;
            Value value;
            if (!parseValue(value)) return false;
            out.object.emplace_back(std::move(key), std::move(value));
            if (consume('}')) return true;
            if (!consume(',')) return false;
        }
    }
    bool parseArray(Value& out) {
        out.type = Value::Type::Array;
        ++mPos;
        skipWs();
        if (consume(']')) return true;
        for (;;) {
            Value value;
            if (!parseValue(value)) return false;
            out.array.push_back(std::move(value));
            if (consume(']')) return true;
            if (!consume(',')) return false;
        }
    }
    bool parseString(std::string& out) {
        skipWs();
        if (mPos >= mText.size() || mText[mPos] != '"') return false;
        ++mPos;
        out.clear();
        while (mPos < mText.size()) {
            char c = mText[mPos++];
            if (c == '"') return true;
            if (c == '\\' && mPos < mText.size()) {
                char e = mText[mPos++];
                switch (e) {
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'u': {
                        // \uXXXX — decode BMP code points (ASCII range is
                        // all the engine formats use; others become '?').
                        unsigned code = 0;
                        for (int i = 0; i < 4 && mPos < mText.size(); ++i) {
                            char h = mText[mPos++];
                            code <<= 4;
                            if (h >= '0' && h <= '9') code += h - '0';
                            else if (h >= 'a' && h <= 'f') code += h - 'a' + 10;
                            else if (h >= 'A' && h <= 'F') code += h - 'A' + 10;
                        }
                        out += (code < 128) ? static_cast<char>(code) : '?';
                        break;
                    }
                    default: out += e; break;
                }
            } else {
                out += c;
            }
        }
        return false;
    }
    bool parseBool(Value& out) {
        out.type = Value::Type::Bool;
        if (mText.compare(mPos, 4, "true") == 0) { out.boolean = true; mPos += 4; return true; }
        if (mText.compare(mPos, 5, "false") == 0) { out.boolean = false; mPos += 5; return true; }
        return false;
    }
    bool parseNumber(Value& out) {
        size_t start = mPos;
        while (mPos < mText.size() &&
               (std::isdigit(static_cast<unsigned char>(mText[mPos])) ||
                mText[mPos] == '-' || mText[mPos] == '+' ||
                mText[mPos] == '.' || mText[mPos] == 'e' || mText[mPos] == 'E')) {
            ++mPos;
        }
        if (start == mPos) return false;
        out.type = Value::Type::Number;
        out.number = std::stod(mText.substr(start, mPos - start));
        return true;
    }

    const std::string& mText;
    size_t mPos = 0;
};

inline std::string escape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 0x0f];
                } else out += static_cast<char>(c);
        }
    }
    return out;
}

} // namespace mini::json
