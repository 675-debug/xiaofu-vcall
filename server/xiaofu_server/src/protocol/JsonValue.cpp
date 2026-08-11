#include "JsonValue.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace {

class Parser {
public:
    // 轻量 JSON 解析器：按当前位置递归读取对象、数组和基础值。
    explicit Parser(const std::string& text) : text(text), position(0) {}
    JsonValue parse(bool& succeeded) {
        succeeded = true;
        JsonValue value = parseValue(succeeded);
        skipWs();
        if (!succeeded || position != text.size()) succeeded = false;
        return value;
    }
private:
    const std::string& text;
    size_t position;
    void skipWs() {
        while (position < text.size() && isspace(static_cast<unsigned char>(text[position])))
            ++position;
    }
    char peek() { skipWs(); return position < text.size() ? text[position] : '\0'; }
    bool consume(char expectedCharacter) {
        skipWs();
        if (position < text.size() && text[position] == expectedCharacter) {
            ++position;
            return true;
        }
        return false;
    }
    JsonValue parseValue(bool& succeeded) {
        const char currentCharacter = peek();
        if (currentCharacter == '{') return parseObject(succeeded);
        if (currentCharacter == '[') return parseArray(succeeded);
        if (currentCharacter == '"') return parseString(succeeded);
        if (currentCharacter == 't' || currentCharacter == 'f' || currentCharacter == 'n')
            return parseLiteral(succeeded);
        if (currentCharacter == '-' || (currentCharacter >= '0' && currentCharacter <= '9'))
            return parseNumber(succeeded);
        succeeded = false;
        return JsonValue();
    }
    JsonValue parseObject(bool& succeeded) {
        JsonValue objectValue;
        consume('{');
        skipWs();
        if (consume('}')) return objectValue;
        while (true) {
            skipWs();
            if (peek() != '"') { succeeded = false; return JsonValue(); }
            const std::string key = parseString(succeeded).asString();
            if (!succeeded || !consume(':')) { succeeded = false; return JsonValue(); }
            const JsonValue value = parseValue(succeeded);
            if (!succeeded) return JsonValue();
            objectValue.set(key, value);
            if (consume('}')) return objectValue;
            if (!consume(',')) { succeeded = false; return JsonValue(); }
        }
    }
    JsonValue parseArray(bool& succeeded) {
        JsonValue arrayValue;
        consume('[');
        skipWs();
        if (consume(']')) return arrayValue;
        while (true) {
            const JsonValue value = parseValue(succeeded);
            if (!succeeded) return JsonValue();
            arrayValue.push(value);
            if (consume(']')) return arrayValue;
            if (!consume(',')) { succeeded = false; return JsonValue(); }
        }
    }
    JsonValue parseString(bool& succeeded) {
        if (!consume('"')) { succeeded = false; return JsonValue(); }
        std::string output;
        while (position < text.size()) {
            const char currentCharacter = text[position++];
            if (currentCharacter == '"') return JsonValue(output);
            if (currentCharacter == '\\') {
                if (position >= text.size()) break;
                const char escapeCharacter = text[position++];
                if (escapeCharacter == 'u') {
                    if (position + 4 > text.size()) { succeeded = false; return JsonValue(); }
                    const unsigned unicodeValue = static_cast<unsigned>(
                        strtoul(text.substr(position, 4).c_str(), nullptr, 16));
                    position += 4;
                    if (unicodeValue < 0x80) output.push_back(static_cast<char>(unicodeValue));
                    else if (unicodeValue < 0x800) {
                        output.push_back(static_cast<char>(0xC0 | (unicodeValue >> 6)));
                        output.push_back(static_cast<char>(0x80 | (unicodeValue & 0x3F)));
                    } else {
                        output.push_back(static_cast<char>(0xE0 | (unicodeValue >> 12)));
                        output.push_back(static_cast<char>(0x80 | ((unicodeValue >> 6) & 0x3F)));
                        output.push_back(static_cast<char>(0x80 | (unicodeValue & 0x3F)));
                    }
                } else {
                    switch (escapeCharacter) {
                        case '"': output.push_back('"'); break;
                        case '\\': output.push_back('\\'); break;
                        case '/': output.push_back('/'); break;
                        case 'b': output.push_back('\b'); break;
                        case 'f': output.push_back('\f'); break;
                        case 'n': output.push_back('\n'); break;
                        case 'r': output.push_back('\r'); break;
                        case 't': output.push_back('\t'); break;
                        default: succeeded = false; return JsonValue();
                    }
                }
            } else {
                output.push_back(currentCharacter);
            }
        }
        succeeded = false;
        return JsonValue();
    }
    JsonValue parseLiteral(bool& succeeded) {
        if (consume('t') && consume('r') && consume('u') && consume('e')) return JsonValue(true);
        if (consume('f') && consume('a') && consume('l') && consume('s') && consume('e')) return JsonValue(false);
        if (consume('n') && consume('u') && consume('l') && consume('l')) return JsonValue();
        succeeded = false;
        return JsonValue();
    }
    JsonValue parseNumber(bool& succeeded) {
        const size_t startPosition = position;
        if (position < text.size() && text[position] == '-') ++position;
        while (position < text.size() && isdigit(static_cast<unsigned char>(text[position]))) ++position;
        if (position < text.size() && text[position] == '.') {
            ++position;
            while (position < text.size() && isdigit(static_cast<unsigned char>(text[position]))) ++position;
        }
        if (startPosition == position) { succeeded = false; return JsonValue(); }
        return JsonValue(strtod(text.substr(startPosition, position - startPosition).c_str(), nullptr));
    }
};

} // namespace

JsonValue::JsonValue() : valueType(Null), boolValue(false), numberValue(0.0) {}
JsonValue::JsonValue(bool value) : valueType(Bool), boolValue(value), numberValue(0.0) {}
JsonValue::JsonValue(double value) : valueType(Number), boolValue(false), numberValue(value) {}
JsonValue::JsonValue(int value) : valueType(Number), boolValue(false), numberValue(value) {}
JsonValue::JsonValue(const std::string& value)
    : valueType(String), boolValue(false), numberValue(0.0), stringValue(value) {}
JsonValue::JsonValue(const char* value)
    : valueType(String), boolValue(false), numberValue(0.0), stringValue(value ? value : "") {}

bool JsonValue::asBool() const { return valueType == Bool ? boolValue : false; }
double JsonValue::asNumber() const { return valueType == Number ? numberValue : 0.0; }
std::string JsonValue::asString() const {
    return valueType == String ? stringValue : std::string();
}

void JsonValue::push(const JsonValue& value) {
    if (valueType != Array) { *this = JsonValue(); valueType = Array; }
    arrayItems.push_back(std::make_shared<JsonValue>(value));
}
size_t JsonValue::size() const {
    if (valueType == Array) return arrayItems.size();
    if (valueType == Object) return objectMembers.size();
    return 0;
}
const JsonValue& JsonValue::at(size_t index) const {
    static const JsonValue kNull;
    return index < arrayItems.size() ? *arrayItems[index] : kNull;
}
void JsonValue::set(const std::string& key, const JsonValue& value) {
    if (valueType != Object) { *this = JsonValue(); valueType = Object; }
    objectMembers[key] = std::make_shared<JsonValue>(value);
}
bool JsonValue::has(const std::string& key) const {
    return valueType == Object && objectMembers.count(key) > 0;
}
const JsonValue& JsonValue::get(const std::string& key) const {
    static const JsonValue kNull;
    const auto memberIterator = objectMembers.find(key);
    return memberIterator != objectMembers.end() ? *memberIterator->second : kNull;
}
std::vector<std::string> JsonValue::keys() const {
    std::vector<std::string> keysList;
    if (valueType == Object) {
        for (const auto& entry : objectMembers)
            keysList.push_back(entry.first);
    }
    return keysList;
}

namespace {
void serializeValue(const JsonValue& value, std::string& output) {
    switch (value.type()) {
        case JsonValue::Null: output += "null"; break;
        case JsonValue::Bool: output += value.asBool() ? "true" : "false"; break;
        case JsonValue::Number: {
            char numberBuffer[64];
            std::snprintf(numberBuffer, sizeof(numberBuffer), "%g", value.asNumber());
            output += numberBuffer;
            break;
        }
        case JsonValue::String: {
            output.push_back('"');
            for (char character : value.asString()) {
                switch (character) {
                    case '"': output += "\\\""; break;
                    case '\\': output += "\\\\"; break;
                    case '\n': output += "\\n"; break;
                    case '\r': output += "\\r"; break;
                    case '\t': output += "\\t"; break;
                    default: output.push_back(character);
                }
            }
            output.push_back('"');
            break;
        }
        case JsonValue::Array: {
            output.push_back('[');
            for (size_t index = 0; index < value.size(); ++index) {
                if (index) output.push_back(',');
                serializeValue(value.at(index), output);
            }
            output.push_back(']');
            break;
        }
        case JsonValue::Object: {
            output.push_back('{');
            const std::vector<std::string> keysList = value.keys();
            for (size_t index = 0; index < keysList.size(); ++index) {
                if (index) output.push_back(',');
                output += "\"" + keysList[index] + "\":";
                serializeValue(value.get(keysList[index]), output);
            }
            output.push_back('}');
            break;
        }
    }
}
} // namespace

std::string JsonValue::serialize() const {
    std::string output;
    serializeValue(*this, output);
    return output;
}

JsonValue JsonValue::parse(const std::string& text, bool* succeeded) {
    bool parseSucceeded = false;
    JsonValue value = Parser(text).parse(parseSucceeded);
    if (succeeded) *succeeded = parseSucceeded;
    return value;
}
