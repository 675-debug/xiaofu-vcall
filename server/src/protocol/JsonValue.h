#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

class JsonValue {
public:
    enum Type { Null, Bool, Number, String, Array, Object };

    JsonValue();
    explicit JsonValue(bool value);
    explicit JsonValue(double value);
    explicit JsonValue(int value);
    explicit JsonValue(const std::string& value);
    explicit JsonValue(const char* value);

    Type type() const { return valueType; }
    bool isNull() const { return valueType == Null; }
    bool isBool() const { return valueType == Bool; }
    bool isNumber() const { return valueType == Number; }
    bool isString() const { return valueType == String; }
    bool isArray() const { return valueType == Array; }
    bool isObject() const { return valueType == Object; }

    bool asBool() const;
    double asNumber() const;
    std::string asString() const;

    void push(const JsonValue& value);
    size_t size() const;
    const JsonValue& at(size_t index) const;

    void set(const std::string& key, const JsonValue& value);
    bool has(const std::string& key) const;
    const JsonValue& get(const std::string& key) const;
    std::vector<std::string> keys() const;

    std::string serialize() const;
    static JsonValue parse(const std::string& text, bool* succeeded = nullptr);

private:
    Type valueType;
    bool boolValue;
    double numberValue;
    std::string stringValue;
    std::vector<std::shared_ptr<JsonValue>> arrayItems;
    std::map<std::string, std::shared_ptr<JsonValue>> objectMembers;
};
