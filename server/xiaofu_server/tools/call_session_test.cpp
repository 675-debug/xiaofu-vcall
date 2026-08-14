// CallSessionManager 行为测试：验证 CallSessionManager 是通话状态唯一事实源。
// 覆盖：呼叫双方 busy、reject/accept/hangup/timeout/disconnect 后的 busy 状态。
#include "../src/handler/CallSession.h"
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
static void check(bool cond, const char* name) {
    if (!cond) { std::printf("FAIL: %s\n", name); ++failures; }
    else std::printf("PASS: %s\n", name);
}

int main() {
    CallSessionManager mgr;
    const std::int64_t now = 1000000;

    // ---------- 1. A 呼叫 B ----------
    CallSession* call1 = mgr.create("call_1", "alice", 11, "bob", 12, now);
    check(call1 != nullptr, "call_request creates session");
    check(call1 != nullptr && call1->isRinging(), "new session is ringing");
    check(mgr.isUserBusy("alice"), "caller busy after call_request");
    check(mgr.isUserBusy("bob"), "callee busy after call_request");
    check(mgr.findActiveForUser("alice") == call1, "findActiveForUser(caller)");
    check(mgr.findActiveForUser("bob") == call1, "findActiveForUser(callee)");

    // 呼叫期间第三方 busy 判断（ServerApp 在 create 前用 isUserBusy 拦截）
    check(mgr.isUserBusy("alice"), "alice still busy while ringing");
    check(mgr.isUserBusy("bob"), "bob still busy while ringing");

    // ---------- 2. B reject ----------
    check(mgr.end("call_1", "rejected", now + 1), "call_reject ends session");
    check(mgr.findByCallId("call_1")->isEnded(), "session state=ended after reject");
    check(!mgr.isUserBusy("alice"), "caller not busy after reject");
    check(!mgr.isUserBusy("bob"), "callee not busy after reject");
    check(!mgr.end("call_1", "rejected", now + 2), "end is idempotent");

    // ---------- 3. B accept（双方继续 busy）----------
    CallSession* call2 = mgr.create("call_2", "alice", 11, "bob", 12, now + 10);
    check(call2 != nullptr, "second call_request ok after reject");
    check(mgr.accept("call_2", now + 20), "call_accept ringing->connecting");
    check(!mgr.accept("call_2", now + 21), "duplicate accept rejected");
    check(mgr.findByCallId("call_2")->state == "connecting", "state=connecting after accept");
    check(mgr.isUserBusy("alice"), "caller busy after accept");
    check(mgr.isUserBusy("bob"), "callee busy after accept");

    // ---------- 4. hangup ----------
    check(mgr.end("call_2", "completed", now + 30), "call_hangup ends session");
    check(!mgr.isUserBusy("alice"), "caller not busy after hangup");
    check(!mgr.isUserBusy("bob"), "callee not busy after hangup");

    // ---------- 5. ringing timeout ----------
    CallSession* call3 = mgr.create("call_3", "alice", 11, "bob", 12, now + 40);
    check(call3 != nullptr, "call_request before timeout");
    const std::vector<std::string> timedOut = mgr.timeoutRinging(30000, now + 70001);
    check(timedOut.size() == 1 && timedOut[0] == "call_3", "timeoutRinging finds ringing call");
    check(mgr.end("call_3", "timeout", now + 70001), "timeout end()");
    check(!mgr.isUserBusy("alice"), "caller not busy after timeout");
    check(!mgr.isUserBusy("bob"), "callee not busy after timeout");

    // ---------- 6. 一方断线：endAllForUser，对端可再次发起 ----------
    CallSession* call4 = mgr.create("call_4", "alice", 11, "bob", 12, now + 80);
    check(call4 != nullptr, "call_request before disconnect");
    const std::vector<std::string> ended = mgr.endAllForUser("bob", "disconnect", now + 90);
    check(ended.size() == 1 && ended[0] == "call_4", "endAllForUser ends bob's active call");
    check(!mgr.isUserBusy("alice"), "peer not busy after disconnect cleanup");
    check(!mgr.isUserBusy("bob"), "disconnected user not busy");
    CallSession* call5 = mgr.create("call_5", "alice", 11, "carol", 13, now + 100);
    check(call5 != nullptr, "peer can call again after disconnect");
    check(mgr.isUserBusy("alice") && mgr.isUserBusy("carol"), "new call marks both busy");

    // ---------- 边界 ----------
    check(mgr.create("bad", "same", 1, "same", 2, now) == nullptr, "create rejects caller==callee");
    check(mgr.create("", "a", 1, "b", 2, now) == nullptr, "create rejects empty callId");
    check(mgr.create("call_5", "dave", 14, "erin", 15, now + 110) == nullptr,
          "create rejects duplicate callId");
    check(mgr.findByCallId("call_5")->caller == "alice",
          "duplicate callId does not overwrite existing session");

    std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
