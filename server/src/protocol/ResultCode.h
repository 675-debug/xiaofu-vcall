#pragma once

// 业务结果码：服务端返回给客户端的统一错误码，0 表示成功
// 客户端可直接展示 code + msg，新增业务错误码时在这里追加
enum class ResultCode : int {
    Ok = 0,              // 成功
    Failed = 1,          // 通用失败：参数缺失、数据库写入失败、非法 JSON 等
    UserNotFound = 2,    // 账号不存在
    WrongPassword = 3,   // 密码错误
    UserExists = 4,      // 账号已存在
    JoinRejected = 5,    // 加入失败：用户名为空或已在线
    InvalidEmail = 6,    // 邮箱格式不正确
    InvalidPassword = 7, // 密码不符合规则：长度 >= 6 且同时包含大小写字母
};
