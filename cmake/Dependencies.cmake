# cmake/Dependencies.cmake — 第三方依赖管理
#
# 规格要求（IMPLEMENTATION_PLAN §2.2）：
#   - 依赖版本必须固定，禁止 main/master/latest
#   - 默认构建不从网络获取浮动最新版
#   - 生成 THIRD_PARTY_NOTICES.md 记录许可证
#
# 当前状态：Phase 0 无外部依赖
# 后续依赖：
#   - standalone Asio (header-only) — Phase 1
#   - mbedTLS (源码编译) — Phase 4
#   - JSON 解析库 — Phase 0（待定）

# Phase 0：无外部依赖，rmt_core 仅用 C++17 标准库
# 后续在此文件固定依赖版本，例如：
# set(ASIO_VERSION "1.30.2")
# set(MBEDTLS_VERSION "3.6.1")
