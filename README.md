# UE5 开放区域物理交互 Demo — 协议优先的环境交互框架

![实机运行截图：火球触发多箱 Chaos 破裂](./Docs/Media/physics-interaction-demo.png)

这是一个基于 Unreal Engine 5.8 的开放区域物理交互垂直切片。项目的核心不是"做了哪些物理效果"，而是：

> **我设计了一套"标准化交互协议 + 分层响应"框架——任何新交互（近战、火球、移动、落地，乃至未来的风、水、植被）只需发布一次标准请求，环境响应零侵入接入；新增一个交互类型的成本，远低于新增一个环境对象的成本。**

角色移动与战斗保留为玩家输入载体（不增加演示专用操作），环境反馈全部由协议驱动。

| 项目项 | 当前定位 |
|---|---|
| 作品集方向 | 大世界交互策划 / 技术美术（物理方向） |
| 核心论点 | 可复用的交互协议框架 + 增量扩展证据 |
| 当前场景 | 营地交互垂直切片（5 箱 + 60 板吊桥 + 常驻杂物区 + 2 个可交互 Lake） |
| 我的职责 | 协议设计、反馈分级、参数体系、C++/Niagara/Chaos 实现、工具链与自动化验收 |
| 技术基线 | Chaos、Geometry Collection、Niagara、Physical Material、Physics Constraint、Water / WaterAdvanced、Niagara Grid2D / Data Channel、Editor Scripting、无头 PIE |

> 当前证明的是"一套可演示、可调参、可回归的环境交互语言"，不用 100m 级测试场地直接宣称"大世界"。World Partition、HLOD、LWC 远原点和多区域性能证据属于后续阶段。

---

## 1. 技术策划的核心论点（每条都配证据）

### 论点 1：协议先于功能 —— 环境交互是"语言设计"不是"功能堆叠"

所有环境交互收敛为**两张协议**，角色与战斗组件只负责发布，不知道任何环境对象的存在：

| 协议 | 职责 | 驱动对象 | 校验/限流 |
|---|---|---|---|
| `FWorldInteractionRequest` | 重交互：命中、伤害、冲量、表面识别 | Chaos 刚体 / Geometry Collection / 贴花 / 音效 | `FGuid` 身份、半径/冲量校验、生命周期上限 |
| `FWorldLightweightInteractionField` | 轻交互：纯表现扰动场 | 常驻 Niagara 粒子（落叶/纸片） | NaN 拒绝、帧预算、按来源每帧限流、EventId 序列 |

证据：近战与火球共用 `FWorldInteractionRequest`，同一套 Subsystem 路由；攻击与移动共用 `FWorldLightweightInteractionField`，同一套校验-限流-分发。**新增交互类型不改协议，只加一个来源枚举值。**

### 论点 2：来源 × 响应解耦 —— 输入侧不知道响应侧，响应侧不反向引用输入侧

```
来源（输入载体）                    响应通道（环境）
─────────────────                ─────────────────
近战 / 火球 / 移动 / 落地  ──→  Chaos 刚体（木箱、吊桥板）
        │                         Geometry Collection 破裂
        │                         Niagara（爆炸、木屑、落叶扰动）
        ▼                         Decal 灼烧痕迹
FWorldInteractionRequest /        音效
FWorldLightweightInteractionField 材质响应（Physical Material / Surface Type）
        │
        ▼
UWorldInteractionSubsystem（路由 + 表面解析 + 共享参数 + 调试统计）
```

- 角色/战斗只发布请求，不持有木箱、吊桥、贴花或 Niagara 的引用；
- 环境对象实现 `UPhysicsInteractable` 接口决定自身响应，Subsystem 负责共享反馈；
- 不用关卡内查找的 `BP_InteractionManager` 伪单例，用随 World 生命周期创建的 `UWorldInteractionSubsystem`。

### 论点 3：增量扩展成本递减 —— 同一协议从单点升级到范围、从玩法升级到表现

| 扩展动作 | 改动范围 | 证据 |
|---|---|---|
| 近战单点 → 火球范围 | 只新增一个来源与一个请求类型 | 火球命中后只提交一次 Explosion 请求，多箱分别响应 |
| 命中 Actor → 空挥也生效 | 把轻量场发布移到命中判定之前 | 刀锋有效帧空挥即扰动落叶，不要求先命中 |
| 移动场 → 攻击/落地/爆炸场 | 只加枚举值和每来源配置 | 4 类来源共用一套校验-限流-分发 |
| 玩法物理 → 表现物理 | 新建轻量协议，不动重协议 | 调落叶尾流不会让吊桥抖动；调木箱冲量不影响落叶密度 |
| 新交互类型（风） | 枚举已预留，直接接线 | `EWorldLightweightInteractionSource::Wind` 已存在，P1 全局风阶段接入 |

### 论点 4：表现物理与玩法物理分离 —— 强度常量不共享

- **玩法物理**（木箱、吊桥）：真实刚体、质量、碰撞、破坏语义，由 Chaos 和 Physics Constraint 负责；
- **表现物理**（落叶、纸片）：放大玩家速度、刀路与冲击，不承担阻挡、伤害和网络权威，由 Niagara 负责。

两层共享玩家输入，但强度常量各自独立：`AttackLooseDebrisStrength` 不复用 `EnvironmentImpulseStrength`，避免调落叶时让木箱或吊桥异常抖动。

### 论点 5：数据驱动 —— 所有数值进 DataAsset，`[PLACEHOLDER]` 纪律

| 配置资产 | 职责 |
|---|---|
| `DA_RoverMovementConfig` | 速度、加速度、转身、急停、空中控制、相机 |
| `DA_RoverCombatConfig` | 每段攻击 Montage、持刀手、伤害、推进、连击窗口、武器 Trace |
| `DA_WorldInteractionConfig` | 世界重力、火球、爆炸、木箱、吊桥、表面响应、贴花生命周期 |
| `DA_WorldLooseDebrisConfig` | 常驻密度、走跑强度、攻击尾流、落地/爆炸、阻尼与静止参数 |
| `DA_WorldWaterRippleConfig` | 水面接收开关、走跑/跳跃/攻击/爆炸映射、半径、限频和浅水模拟预算 |

未定稿数值标 `[PLACEHOLDER]` 并附验证路径；常规调参改 DataAsset，代码中不出现手感常量。

---

## 2. 复用矩阵：哪些来源 × 哪些响应通道已接通

| 来源 \ 响应 | Chaos 刚体/GC | 常驻落叶粒子 | WaterAdvanced | Decal | 表面识别 | 请求-确认模式 | 武器/攻击状态 |
|---|---|---:|---:|---:|---:|---:|---:|
| 移动（走/跑/冲刺） | — | ✅ | ✅ 连续身体碰撞 | — | — | — | — |
| 跳跃 / 落地 | ✅（吊桥承重/冲击） | ✅ | ✅ 独立强度 | — | — | — | — |
| 近战直击 | ✅（一刀破箱） | ✅（空挥也扰动） | ✅ 定向冲击 | — | ✅ Wood / Water | ✅ | ✅ |
| 火球爆炸 | ✅（范围多箱） | ✅ | ✅ 范围冲击 | ✅（灼烧） | ✅ Wood / Water | — | — |
| 地面攻击四段 / 重击 | — | — | — | — | — | ✅（请求-确认+Watchdog） | ✅ |
| **Wind（预留）** | P1 | P1 | P1 | — | — | — | — |
| **Cloth / 草地** | P2 | P2 | — | — | 槽位已建 | — | — |

已建槽位但未完成差异化：Stone / Metal / Grass / Water / Cloth 的物理材质与响应协议已就位，Wood 链路最完整，其余视觉差异在 P1 打磨。

---

## 3. 案例：每个案例证明"复用"的一个维度

### 案例 A：常驻、可反复交互的地表轻质杂物 —— 证明"表现物理复用轻量协议"

落叶和纸片属于场景，而不是技能附属粒子。区域内维持约 `450` 个 Ambient 粒子的稳态预算，移动、攻击、落地和爆炸只扰动**环境中已经存在的粒子**，不为每次交互重新生成 Niagara System（无头 PIE 断言 `interaction_systems=0`）。

**设计要点**：

- 攻击不是单一排斥力：地下/后方排斥力负责掀起贴地粒子，刀路前上方吸引力负责沿攻击方向牵引，尾流独立 `0.20s` 计时；
- 武器 Trace 可有多个刀身采样和时间子步，但 Niagara 限制为每攻击来源每帧一个场——**判定精度不直接放大表现成本**；
- Ambient 粒子保持"可再次唤醒"：旋转驱动力 `0/0`、旋转阻尼 `8`、Restitution `0`，避免永久自转，同时保留下一次 Point Force 响应；
- 镜头向下曾整片消失：根因是粒子超出 Niagara Fixed Bounds 后被整套裁剪，改为 CPU Dynamic Bounds 解决；
- 完整设计、参数和失败复盘见 [2026-08-09 交互设计日志](./Docs/InteractionDesignLog-2026-08-09.zh-CN.md) 与 [Niagara 地表轻质杂物交互系统说明](./Docs/Niagara地表轻质杂物交互系统方案.md)。

### 案例 B：一刀破坏木箱 —— 证明"重协议 + 表面识别"的完整链路

木箱是"攻击环境"的最基础教学。真实刀身 Sweep 在 Active 窗口扫到箱体即触发破坏：

- 完整态 = 开启 Simulate Physics 的 StaticMesh 刚体，质量基线 `80kg`，世界重力统一 `-980cm/s²`；
- 破裂时把 Transform、线速度、角速度交给 Geometry Collection（16 个 Voronoi rigid leaves，单根 Cluster）；
- `OnChaosBreakEvent` 只在真实破裂后触发木屑 Niagara，不用预设时间假装破裂；
- **Fracture 资产优化**：定位到 PlanarCut 无噪声面按 `1cm` 重拓扑导致内部面 `245,992`，间距调为 `100cm` 后降至 `412`，资产 `22.1MB → 168KB`，碎片结构不变——这是"用数据定位问题"的代表案例；
- 调试入口：`rover.combat.DrawAttackTrace 0/1`（未命中/命中 Sweep、刀根/刀尖分色）。

### 案例 C：火球与多目标范围反馈 —— 证明"同一协议从单点升级为范围事件"

近战强调时机与方向，火球强调范围与同时响应，**二者共享环境协议，不为每个技能复制一套木箱逻辑**：

- 投射物按相机视线飞行，命中后只提交一次 Explosion 请求；
- Subsystem 解析 Physical Material / Surface Type，范围内对象各自决定破裂、受冲量或忽略；
- 爆炸 Niagara、木屑、灼烧贴花、投射物、碎片均有数量或时间上限；
- 同一次爆炸向常驻杂物发布纯表现轻量场，不把 Niagara 强度反写到木箱或吊桥。

### 案例 D：保留攻击位移的 60 板动态吊桥 —— 证明"同一物理路径拆分语义，避免相互污染"

吊桥是开放区域中的动态通行节点：`60` 块独立物理木板、`4` 个端点挂点、`122` 个约束，桥面约 `16.77m x 4.00m`。

**这轮解决的关键不是把桥调硬，而是拆开不同物理语义**：

| 体验问题 | 当前规则 / 证据 |
|---|---|
| 桥是否像可通行路径 | 60 板，约 `16.77m x 4.00m` |
| 是否有自然弧线 | 初始下垂 `80cm` |
| 是否有重量 | 单板 `15kg`，角色持续承重 |
| 跑步是否强于走路 | 最新输入冲量 `125.1 > 46.9` |
| 离开后是否无限晃 | 10 秒后速度归零，自然姿态误差 `1.13°` |
| 是否会横向扭曲 | 59 对双侧板缝约束抑制横滚 |
| 攻击是否保留位移 | 相对脚下板向攻击方向前移 `18.7cm` |
| 攻击是否会盖过落地 | Attack `158.8cm/s`，Landing `309.2cm/s`，比值 `0.514` |

攻击推进使用平滑 `ConstantForce`（保留 `0.55` 距离、`2.50` 时长缩放、完整 Ease）；桥板 DirectHit 由接收者按 `0.05` 倍、最大 `50` 自行消费；木箱通用冲量和 Explosion 径向冲量保持独立——**推进 / DirectHit / 通用冲量 / Explosion 四路不共用强度常量**。完整说明见 [物理吊桥系统说明](./Docs/物理吊桥系统说明.md)。

### 案例 E：两个现有 Lake 的 WaterAdvanced 浅水交互 —— 证明“持续接触与离散事件可以进入同一环境反馈层”

主 Demo 保留场景已经摆好的两个 `WaterBodyLake`，不改 Transform、Spline 或基础材质。每个 Lake 配置一个 `AWorldWaterRippleRegion` 负责空间过滤，最终共享 WaterAdvanced 的玩家跟随 Grid2D 浅水模拟。

**水面输入按行为语义拆成两层**：

| 输入层 | 行为 | 实现 |
|---|---|---|
| 连续身体碰撞 | 行走、奔跑、涉水位移 | WaterAdvanced 读取 `PHYS_Rover_Male` 的 14 个主要身体碰撞体 |
| 标准化一次性冲击 | 起跳、落地、近战、爆炸 | Region 将 Position / Velocity / Radius 转换后调用 `RegisterImpact` |

排查过程中出现过“攻击有涟漪、移动没有”的典型假闭环：攻击链路独立有效，但角色 SkeletalMesh 原本没有 Physics Asset，WaterAdvanced 无法生成连续 Collider。最终不仅补齐 Physics Asset，还把资产绑定、Body 数量和运行时 `RigidMesh_ShallowWaterCollider` 标签写进专项验证，避免以后只验证“请求发出了”。

当前局部浅水窗口为 `2400cm / 512`，停止交互后保持 `15s`；走跑、跳跃、落地、攻击和爆炸都有独立速度与半径参数。完整复盘见 [2026-08-10 WaterAdvanced 双湖交互设计日志](./Docs/InteractionDesignLog-2026-08-10-WaterAdvanced.zh-CN.md) 与 [鸣潮水面复刻实现方案](./Docs/鸣潮水面复刻实现方案.md)。

---

## 4. 扩展点清单：新交互如何搭现有协议的车

这是"以现有功能做拓展交互"的直接落点。以下每一项新增时，**角色与战斗组件零改动**：

| 扩展点 | 当前状态 | 接入方式 |
|---|---|---|
| `EWorldLightweightInteractionSource::Wind` | 枚举已存在，无生产者 | P1 全局风阶段发布 Wind 场，Region 消费逻辑已兼容 |
| 表面响应槽位 | 6 类 Physical Material，Wood 完成 | 在 `DA_WorldInteractionConfig` 的 SurfaceResponses 填 Niagara/音效/贴花 |
| 新环境对象 | 接口就绪 | 实现 `UPhysicsInteractable` 一个接口即可被请求路由 |
| Niagara Data Channel | 协议已建立并写入 | 后续 GPU 扩量/Islands 时由 Niagara 端消费，Region 当前走 Subsystem 委托 |
| 新攻击段特效 | 挂点复用 | 复用现有 `RoverAttackActiveBegin/End` Notify + 命中广播，不新增 Montage 改动（见 [攻击空间扰动特效实现方案](./Docs/攻击空间扰动特效实现方案.md)，已规划待实现） |
| 请求-确认-握手 + Watchdog | 移动转身/急停/战斗三处复用 | 新动画状态照抄该模式，自带超时兜底 |

**工具链同样是可复用的**：资产生成（`Configure*.ps1`）幂等、只做版本化迁移、不覆盖手调值；验证（`Validate*.ps1`）无头 PIE 专项断言。新增一个系统 = 新增一个专项脚本，管线本体不再重写。

---

## 5. 验收：自动化证明协议，有渲染证明手感，两者不可互相替代

| 验收层 | 当前检查内容 |
|---|---|
| Build | Runtime / Editor 模块在 UE 5.8 编译通过 |
| 基础角色 PIE | GameMode、Pawn、输入、移动、跳跃和相机不回归 |
| 木箱专项 | 一刀破坏、质量/重力、GC 接管、表面解析和清理 |
| 吊桥专项 | 板数、约束数、误差、承重、恢复和输入来源诊断 |
| Loose Debris 专项 | 静止不发场、移动/空挥/落地/爆炸、攻击尾流、覆盖、限流、`interaction_systems=0` |
| WaterAdvanced 双湖专项 | 子系统 / Grid2D、双 Lake 绑定、Collider 标签、走跑/跳跃/攻击/爆炸、跨 Region 过滤 |
| 视觉检查 | 密度、方向、穿地、持续旋转、落地再激活、走跑和落地强度层级 |
| 性能检查 | Niagara Debugger、`stat Niagara`、Insights、固定路线 p50/p95；尚待正式基线 |

2026-08-09 本机回归记录：

| 脚本 | 关键证据 | 结果 |
|---|---|---|
| `BuildEditor.ps1` | Runtime / Editor 模块，UE 5.8 Development Editor | 通过 |
| `ValidatePhysicsWorldLooseDebrisPIE.ps1` | 移动 `1`、攻击 `2`、起跳 `21`、落地 `1`、爆炸 `1`；`interaction_systems=0`、NDC 写入 `26` | 通过 |
| `ValidatePhysicsWorldBoxPhysicsPIE.ps1` | 完整箱/GC `80kg`、重力 `-980cm/s²`、GC 接管连续、碎片展开 `20.1cm` | 通过 |
| `ValidatePhysicsWorldRopeBridgePIE.ps1` | `60` 板 / `122` 约束；Attack/Landing 比值 `0.514`；相对推进 `18.7cm`；恢复 `0/0` | 通过 |
| `ValidateRoverPIE.ps1` | GameMode、Pawn、输入映射与跳跃冒烟 | 通过 |

2026-08-10 水面专项记录：

| 脚本 | 关键证据 | 结果 |
|---|---|---|
| `BuildEditor.ps1` | Water / WaterAdvanced Runtime 与 PhysicsUtilities Editor 工具编译 | 通过 |
| `ValidatePhysicsWorldDualLakeDemoAssets.ps1` | 1 WaterZone、2 WaterBodyLake、2 Region；Physics Asset 绑定与 14 Bodies | 通过 |
| `ValidatePhysicsWorldDualLakePIE.ps1` | `2400/512` Grid；`rover_collider=14bodies/tagged`；Movement / Jump / Attack / Landing / Explosion 各 2；跨区 0 | 通过 |
| 人工有渲染 PIE | 走跑、跳跃、落地和攻击均能驱动现有 Lake 水面 | 通过 |

角色综合 `ValidateRoverPIE.ps1` 当前仍会停在既有 ground jump 位移阈值；水面迭代未修改用户已调好的跳跃参数，该失败与 WaterAdvanced 无关。

数字证明协议、行为层级和资产状态可回归，不等于最终 VFX 画面或大世界性能已验收。最新分阶段规划见 [Roadmap](./Docs/Roadmap.zh-CN.md)。

---

## 6. 这次迭代纠正了什么（技术决策复盘）

| 表现问题 | 原因 | 处理 |
|---|---|---|
| 每次交互重新出现粒子 | 把环境误做成事件 Burst | 常驻 Ambient 总量，事件只扰动已有粒子 |
| 玩家所有行为都没反馈 | Niagara 模块绑定失败仍被工具当成成功 | 绑定失败向上传播，配置脚本直接失败 |
| DataAsset 手调不生效 | 旧资产值、Rapid Iteration 与 User 参数混用 | 统一运行时 User 参数，一次性资产 Schema 迁移 |
| 地上的粒子无法再次响应 | 一次性 Rest/Burst 生命周期与常驻需求冲突 | 阻尼 + Calming 静止，保留可再次受力 |
| 攻击只有圆形排斥 | 缺少沿刀路的方向目标 | 增加独立前上方吸引尾流 |
| 爆炸参数变强却覆盖不到粒子 | 力源偏移到了自身半径外 | Point Force 原点限制在 `0.8R` 内 |
| 镜头向下时整片杂物消失 | 世界空间粒子超出 Fixed Bounds 后整套被视锥裁剪 | CPU Emitter 改 Dynamic Bounds |
| 桥上普通攻击接近落地强度 | 推进载荷、桥板直击、通用环境冲量共用物理路径 | 拆分推进/DirectHit/通用冲量/Explosion，增加桥侧战斗阻尼窗口 |
| 攻击水面有效但走跑无反馈 | 自动化只证明 `RegisterImpact`，角色 Mesh 没有 Physics Asset，连续 Collider 根本未建立 | 生成 14 Bodies 的 `PHYS_Rover_Male`，并验证运行时 Collider 标签 |

共同标准：不靠缩短寿命或隐藏对象掩盖问题，而是找到**输入、空间覆盖、能量来源、生命周期**之间的真实矛盾。

---

## 7. 操作方式

| 输入 | 功能 |
|---|---|
| `WASD` | 移动并影响地表轻质杂物 |
| 鼠标 | 自由观察 |
| `Left Shift` | 奔跑 |
| `Space` | 跳跃 / 二段跳，起跳与落地发布独立环境反馈 |
| 鼠标左键 | 近战（四段定向 + 重击/鸣奏）；刀锋有效帧同时影响木箱与常驻杂物 |
| `WASD + 攻击` | 每段重新选择攻击方向 |
| `Q` | 发射火球并产生范围爆炸 |

角色进入场景内两个 Lake 后无需额外按键：走、跑、跳、落地、近战和火球爆炸会按各自强度影响水面。

调试入口：`pw.LooseDebris.DrawFields 0/1`（交互场可视化）、`rover.combat.DrawAttackTrace 0/1`（武器 Sweep 可视化）。

---

## 8. 文档索引

- [阶段路线图（P0→P4）](./Docs/Roadmap.zh-CN.md)
- [项目介绍与阶段总结（面试材料/讲解提纲）](./Docs/开放区域物理交互Demo_项目介绍与阶段总结.md)
- [2026-08-09：Niagara 常驻地表杂物交互复盘](./Docs/InteractionDesignLog-2026-08-09.zh-CN.md)
- [2026-08-09：吊桥攻击响应分层复盘](./Docs/InteractionDesignLog-2026-08-09-RopeBridgeAttackResponse.zh-CN.md)
- [2026-08-10：WaterAdvanced 双湖浅水交互复盘](./Docs/InteractionDesignLog-2026-08-10-WaterAdvanced.zh-CN.md)
- [Niagara 地表轻质杂物交互系统方案](./Docs/Niagara地表轻质杂物交互系统方案.md)
- [物理吊桥系统说明](./Docs/物理吊桥系统说明.md)
- [鸣潮水面复刻实现方案](./Docs/鸣潮水面复刻实现方案.md)
- [攻击空间扰动特效实现方案（已规划）](./Docs/攻击空间扰动特效实现方案.md)
- [可交互水面调研（P2 前置）](./Docs/可交互水面调研文档.md)

<details>
<summary><strong>构建与验证</strong></summary>

要求：Windows 10 / 11、Unreal Engine 5.8、Visual Studio 2022、Git LFS。

```powershell
git lfs install
git clone https://github.com/GoodNatGod/UE5-Physics-Interaction-Demo.git
cd UE5-Physics-Interaction-Demo

powershell -File .\Scripts\BuildEditor.ps1 `
  -EngineRoot "C:\Program Files\Epic Games\UE_5.8" `
  -StageRoot "$env:TEMP\PhysicsWorldDemoBuild" `
  -SkipSync
```

常用验证入口：

```powershell
powershell -File .\Scripts\ConfigurePhysicsWorldLooseDebris.ps1 -EngineRoot <UE5.8-path>
powershell -File .\Scripts\ValidatePhysicsWorldLooseDebrisPIE.ps1 -EngineRoot <UE5.8-path>
powershell -File .\Scripts\ValidatePhysicsWorldBoxPhysicsPIE.ps1 -EngineRoot <UE5.8-path>
powershell -File .\Scripts\ValidatePhysicsWorldP0PIE.ps1 -EngineRoot <UE5.8-path>
powershell -File .\Scripts\ValidatePhysicsWorldRopeBridgePIE.ps1 -EngineRoot <UE5.8-path>
powershell -File .\Scripts\ConfigurePhysicsWorldDualLakeDemo.ps1 -EngineRoot <UE5.8-path>
powershell -File .\Scripts\ValidatePhysicsWorldDualLakeDemoAssets.ps1 -EngineRoot <UE5.8-path>
powershell -File .\Scripts\ValidatePhysicsWorldDualLakePIE.ps1 -EngineRoot <UE5.8-path>
powershell -File .\Scripts\ValidateRoverPIE.ps1 -EngineRoot <UE5.8-path>
```

部分 PIE 验证依赖未公开的合法角色资产与完整测试关卡，因此公开仓库主要用于作品集审阅、规则复盘、自制物理资产和源码检查。

</details>

## 资产与授权

本仓库只公开源码、工具、自制物理内容和演示截图，不包含第三方角色、动作、贴图、武器或原始 FBX。相关角色与商标归各自权利人所有；本项目与相关游戏开发商无隶属或授权关系。详见 [THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md)。
