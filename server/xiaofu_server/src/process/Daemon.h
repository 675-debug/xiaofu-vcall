#pragma once

// 仅在显式传入 --daemon 时执行传统 Unix 双 fork；systemd 默认使用前台模式。
bool daemonizeProcess();
