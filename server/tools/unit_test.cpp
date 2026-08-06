#include "../src/protocol/JsonValue.h"
#include "../src/db/PasswordHasher.h"
#include <cstdio>

static int failures = 0;
static void check(bool cond, const char* name) {
    if (!cond) { std::printf("FAIL: %s\n", name); ++failures; }
    else std::printf("PASS: %s\n", name);
}

int main() {
    bool ok = false;
    JsonValue v = JsonValue::parse("{\"type\":\"login\",\"username\":\"alice\",\"code\":0,\"ok\":true,\"arr\":[1,2,3]}", &ok);
    check(ok, "json parse ok");
    check(v.isObject() && v.get("type").asString() == "login", "json string");
    check(v.get("code").asNumber() == 0, "json number");
    check(v.get("ok").asBool() == true, "json bool");
    check(v.get("arr").isArray() && v.get("arr").size() == 3, "json array");
    check(v.get("arr").at(2).asNumber() == 3, "json array element");

    JsonValue out;
    out.set("type", JsonValue("heartbeat_resp"));
    out.set("code", JsonValue(0));
    out.set("msg", JsonValue("ok"));
    bool ok2 = false;
    JsonValue back = JsonValue::parse(out.serialize(), &ok2);
    check(ok2 && back.get("code").asNumber() == 0, "json round trip");

    check(PasswordHasher::sha256Hex("123456") ==
          "8d969eef6ecad3c29a3a629280e686cf0c3f5d5a86aff3ca12020c923adc6c92", "sha256 123456");
    check(PasswordHasher::sha256Hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 empty");

    std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}