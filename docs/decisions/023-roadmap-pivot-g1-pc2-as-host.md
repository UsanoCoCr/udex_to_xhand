# ADR-023: Pivot deployment host from external Linux PC to Unitree G1 PC2

Date: 2026-05-16
Status: Accepted

## Context

M0–M4 已在一台外置 Linux 开发 PC 上完成验证：
- M1 UDP 接收正常
- M2 UDCAP 参数映射通过官方文档锁定（ADR-009/010/011/012/013）
- M3 XHand SDK 经 CDC-ACM 串口跑通（ADR-014–019）
- M4 单手端到端遥操作通过（ADR-020–022）

原 roadmap (`docs/plans/00-roadmap.md`) 默认 Linux 主机是一台独立机器，
通过 USB-RS485 接 XHand，并从 Windows UDCAP 机器收 UDP。

近期我把宇树 G1 人形机器人的板载副计算机（PC2）刷成了 Linux。
PC2 物理上集成在机器人本体，是后续 onboard 操作控制的天然宿主。
继续依赖外置 Linux PC 意味着永久多一个盒子、多一捆线缆，且与最终 onboard 部署形态不一致。
现在切换的代价小，越往后做切换代价越高。

PC2 的 CPU 架构为 aarch64。

## Decision

将部署目标从「任意外置 Linux PC」改为「宇树 G1 PC2」。

Roadmap 调整（原 M5/M6/M7 全部后移一位为 M6/M7/M8）：

- **M0–M4 维持 ✅**：代码层面工作完成。但需在文档里区分「在外置 PC 上验证过」≠「在 G1 PC2 上部署过」。
- **新增 M5: Port to G1 PC2**（~0.5d）
  - 确认 PC2 上有 Python 3.10+，建 conda 环境
  - 获取/编译匹配 aarch64 的 `xhand_controller` wheel（**这是 M5 的前置硬关卡**，wheel 不可用则 M5 阻塞、需要单独 ADR 决策替代路径）
  - 把 XHand 接到 PC2 USB，验证 `/dev/ttyACM*` 枚举 + 串口权限
  - 配置网络路径：Windows UDCAP → G1 网络 → PC2（可能需要静态 IP / 防火墙放行）
  - 在 PC2 上重跑 M3、M4 验收，记录差异
- **M6 Safety**（原 M5）：算法不变，但补一条「响应 SIGTERM 等同于 Ctrl+C」，因为 PC2 可能被机器人整体生命周期管理。
- **M7 Dual hand**（原 M6）：算法不变；硬件前置检查改为「PC2 暴露的 USB 数量 + RS485 双手寻址方案」。
- **M8 Tuning + 验收**（原 M7）：验收测试改为「XHand 装在 G1 手臂末端」而非手持。

明确排除在本次 pivot 范围外：
- 接入宇树 ROS / SDK（CLAUDE.md 禁止 ROS2）
- 手臂控制（CLAUDE.md 禁止）
- 用宇树内部 DDS 替换 UDP——保留 UDP 以保护 M1 的已验证解析栈

## Consequences

**收益：**
- 去掉外置 Linux PC，硬件部署更简单
- 控制器与手臂同机，USB 线更短，潜在抖动更低
- 为未来 onboard 自主操作铺路，避免二次迁移

**风险与代价：**
- **aarch64 wheel 可用性是最大未知数**：现有 `xhand_controller` wheel 若只有 x86_64 版会直接阻塞 M5。必须先验证 wheel 可用性，再投入 M5 后续工时；不可用则需另开 ADR 决策（vendor 协助 / 源码编译 / 回退外置 PC）。
- M3/M4 的「在我笔记本上能跑」证据不会自动转移到 PC2，所有硬件相关验证都要重做。
- PC2 的 CPU/RAM/USB 供电预算可能比开发 PC 紧，原本看不出的问题会暴露。
- Windows UDCAP 机器到 PC2 的网络路径是新的，可能要配静态 IP / 路由 / 专用链路。

**时间线影响：** +0.5d (M5)。原最快路径 4d → 4.5d。若 wheel 架构问题成立，M5 工期会膨胀并阻塞 M6/M7。

## Alternatives Considered

1. **维持外置 Linux PC**。最省事，所有已有验证都不动。拒绝原因：永久增加一个盒子，与最终机器人形态不符，迁移成本随时间增长。
2. **(选中)** PC2 跑控制器，UDCAP 路径保持不变。
3. **用宇树内部 IPC (DDS) 替换 UDP**。拒绝：UDCAP HandDriver 在 Windows 上只能发 UDP，源头改不了；保留 UDP 也保护 M1 已验证的解析器；跨平台 IPC 无当下收益。
4. **跳过 M5，把 PC2 迁移塞进 M8**。拒绝：把宿主机迁移和 PID 调参绑在一起，验收失败时分不清是宿主问题还是算法问题。
