#pragma once

// 客户端与服务端共享的协议结果码（只放客户端需要判断的稳定值）。
// 同步自 server/xiaofu_server/src/protocol/ResultCode.h，
// 不要在这里用魔法数字，避免客户端/服务端协议码再次漂移。
namespace Protocol {
inline constexpr int Ok = 0;                      // ResultCode::Ok
inline constexpr int Failed = 1;                  // ResultCode::Failed
inline constexpr int UserNotFound = 2;            // ResultCode::UserNotFound
inline constexpr int WrongPassword = 3;           // ResultCode::WrongPassword
inline constexpr int AccountAlreadyLoggedIn = 16; // ResultCode::AccountAlreadyLoggedIn
                                                  // DuplicateLogin=15 是被踢下线，不能用于登录被拒判定
}
