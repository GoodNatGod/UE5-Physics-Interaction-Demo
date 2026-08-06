# 大世界物理交互 Demo 开发规划

> 版本：v0.4.0
> 日期：2026-08-06
> 定位：大世界表现与交互 / 技术美术（物理方向）作品集垂直切片

## 1. 目标变更

项目不再以“完整复刻《鸣潮》漂泊者玩法”为最终目标。现有漂泊者移动、相机、地面 AAA、武器、命中与受击系统作为稳定的玩家交互底座，后续主线改为：

> 在 UE 5.8 中制作一个可演示、可度量、可扩展的开放区域物理交互 Demo，展示战斗输入如何统一驱动 Chaos、Niagara、材质、植被、水体与大世界加载系统。

最终交付包括：

- 一套可扩展的环境交互框架；
- 一个废弃营地主题的可玩垂直切片；
- 一条完整的“技能命中 → 环境物理响应 → 视觉反馈”展示链路；
- 性能与精度取舍数据；
- 约 3 分钟演示视频、README 和面试讲解材料。

## 2. 范围判断

### 2.1 保留

- `ARoverCharacter`、`URoverLocomotionComponent`、`URoverCombatComponent` 与现有动画资产；
- 地面 AAA、武器 Trace、伤害和受击反馈；
- C++ 核心逻辑 + 蓝图/AnimBP/Niagara 表现的混合架构；
- 自动化资产生成、BuildEditor 与无头 PIE 验证流程。

### 2.2 暂停

- 完整角色复刻、更多连招、跳跃攻击、闪避、攀爬、钩锁等角色系统；
- 与物理交互作品集没有直接展示价值的动画手感扩展；
- 在核心闭环完成前堆叠水体、布料、PCG、HLOD 等孤立功能。

### 2.3 不采用

- 关卡中放置并全局查找 `BP_InteractionManager` 的伪单例；
- 让角色、技能、木箱、贴花和 Niagara 互相直接引用；
- 用 100m × 100m 场景本身作为“大世界”证明；
- 只有视频效果、没有参数来源和性能数据的不可复现实现。

## 3. 核心架构

```text
Rover Character / Combat
        │ 标准化 Hit / Skill 请求
        ▼
UWorldInteractionSubsystem
        │
        ├── Surface Resolver（物理材质 / Surface Type）
        ├── Physics Response（Chaos Field / Geometry Collection）
        ├── VFX Response（Niagara）
        ├── Material Response（Decal / MPC / WPO）
        └── World Response（Wind / Water / Foliage）
                    │
                    ▼
        UPhysicsInteractable 实现对象
```

### 3.1 Runtime 类型

- `EWorldElementType`：Physical、Fire、Wind、Water 等交互元素；
- `FWorldInteractionRequest`：来源、命中结果、元素、伤害、冲量、半径、标签；
- `UPhysicsInteractable`：环境对象接收请求的统一接口；
- `UWorldInteractionSubsystem`：当前 World 内的交互路由、全局参数和调试统计；
- `UWorldInteractionConfig`：表面映射、爆炸衰减、贴花生命周期、风力等数据。

### 3.2 边界

- 角色与战斗组件只提交请求，不知道木箱、篝火或贴花 Actor；
- 可交互 Actor 决定自身状态变化，Subsystem 负责共享反馈和调度；
- Niagara、材质、Chaos 参数通过 DataAsset、MPC 或 Niagara Parameter Collection 注入；
- 所有新资产放入 `Content/PhysicsWorldDemo/`，与 `Content/Rover/` 分离。

## 4. 分阶段路线

### P0：交互闭环

目标：证明现有战斗底座可以稳定驱动环境反馈。

1. 创建交互枚举、请求结构、接口、Subsystem 与 Config；
2. 添加火球技能输入和投射物，命中后产生标准化请求；
3. 创建可破坏木箱：生命值、分级破坏、Geometry Collection、木屑反馈；
4. 建立 Stone/Wood/Metal/Grass 等表面映射；
5. 添加爆炸 Niagara、Chaos 径向冲击和灼烧贴花；
6. 建立专项 PIE：验证请求、表面分流、伤害、破坏、贴花生命周期和清理。

验收场景：玩家发射火球命中木箱，木箱受伤/破碎，同时产生木屑、爆炸冲击和灼烧痕迹；命中石地时切换为石质反馈且不触发木箱逻辑。

### P1：环境表现

目标：展示统一环境参数影响不同系统。

1. 篝火：火焰、烟雾、余烬、动态光；
2. 全局风：统一驱动 Niagara、旗帜、营帐和植被材质；
3. Chaos Cloth 旗帜与营帐，补充碰撞和极端参数保护；
4. 调试面板显示风向、风强、活跃粒子和活动物理对象数量。

### P2：地表与生态交互

目标：加入角色和环境的持续接触反馈。

1. 草地 WPO 弯曲与恢复；
2. Water Plugin 水体、角色进入涟漪和火球命中水花；
3. 火、风、水之间最少一组组合反应；
4. 统一调试可视化与对象池策略。

### P3：PCG 与大世界证据

目标：用数据证明可扩展性，不只展示一块小营地。

1. PCG 生成树林、草地和石头，并支持营地排除区；
2. 扩展测试区域尺寸 `[PLACEHOLDER]`，启用 World Partition/Data Layer；
3. 配置 HLOD、加载距离与实例化策略；
4. 记录不同视距下 Actor/实例数量、Game/GPU 帧时间、内存与 Chaos/Niagara 开销；
5. 给出质量优先与性能优先两套配置对比。

### P4：作品集交付

1. 完成场景光照、镜头路线和交互引导；
2. 制作 3 分钟演示：目标、架构、交互闭环、性能对比、工具与管线；
3. README 记录架构图、资产管线、关键参数和复现步骤；
4. 整理每个模块的原理、取舍、故障案例与优化结果。

## 5. 技术可行性与风险

| 项目 | 结论 | 主要风险 |
|---|---|---|
| Niagara 火焰/爆炸 | 可行 | 高质量材质与贴图仍需美术资产和人工调参 |
| Chaos Destruction | 可行 | Fracture、碰撞层级和碎片数量直接影响稳定性与性能 |
| Chaos Cloth | 可行 | Cloth Asset、权重绘制、碰撞体通常需要编辑器内人工处理 |
| 表面/贴花反馈 | 高可行 | 需要统一 Physical Material 资产规范与对象池 |
| 草地 WPO | 高可行 | 大范围 MPC/RVT 写入方式需评估精度与成本 |
| Water | 可行 | 插件依赖、材质成本与角色移动模式需隔离 |
| PCG | 可行 | 必须准备可用植被资产、碰撞和 Nanite/HISM 策略 |
| World Partition/HLOD | 可行 | 100m 场景无法形成有说服力的流送与 HLOD 数据 |

原文“约 11 天完成全部系统”只适合功能原型，不适合作品集质量承诺。正式排期应以 P0 闭环通过自动化验证后，再根据美术资产准备情况评估。

## 6. P0 实施状态

本机 UE 5.8 插件基线：Niagara 与 PCG 为默认启用；Geometry Collection、Chaos Cloth Asset 与 Water 需要工程显式启用，Water 仍标记为 Experimental。P0 只启用实际需要的 Niagara、Geometry Collection/Chaos Solver 依赖，其他插件随阶段开启。

当前已按以下顺序完成 P0，不先做布料：

1. 已启用并验证 P0 所需插件/模块；
2. 已建立 `UWorldInteractionSubsystem`、请求结构、接口与 DataAsset；
3. 已把现有武器 Trace 的非角色命中转换为交互请求；
4. 已实现 `Q` / 手柄右肩键火球、投射物与爆炸请求；
5. 已创建开启刚体模拟的可破坏木箱、16 碎片单根簇 Geometry Collection、Physical Material、Niagara 和灼烧贴花；
6. 完整箱与破裂 Geometry Collection 共用逐实例质量语义，世界重力由 `DA_WorldInteractionConfig` 统一下发；
7. 已增加幂等资产生成、Build 和专项 PIE 验证脚本。

当前 Geometry Collection、Niagara 与灼烧贴花均为可替换的 P0 占位资产，不作为最终美术质量交付。专项 PIE 通过后，再进入 Niagara 质量打磨和 Chaos Cloth。

## 7. 对原始需求文档的修订结论

| 原始方案问题 | 改进方式 |
|---|---|
| 以功能清单和约 11 天排期驱动，核心链路可能最后才连通 | 先完成可玩的 P0 垂直切片，再按 P1-P4 扩展 |
| 关卡内 `BP_InteractionManager` / `BP_ImpactManager` 形成查找式伪单例 | 使用随 World 创建和销毁的 `UWorldInteractionSubsystem` |
| 技能、木箱、Niagara、贴花彼此直接引用 | 用 `FWorldInteractionRequest` 和 `UPhysicsInteractable` 隔离生产者与响应者 |
| 缺少请求身份、非法输入保护、生命周期与数量上限 | 请求使用 `FGuid`；校验半径/伤害/冲量；投射物、碎片和贴花均有清理策略 |
| 验收项多为“看起来有效”，无法回归 | 增加资产生成脚本、专项 PIE、统计计数和明确成功标记 |
| 100m × 100m 营地直接被称为大世界 | 营地只证明交互闭环；World Partition/HLOD 必须在 P3 扩区并提交测量数据 |
| 性能描述只有 `stat unit` 等命令，没有测试条件 | 固定硬件、构建配置、相机路线、样本时长和 p50/p95 指标后再下结论 |

## 8. P0 验收与证据

### 8.1 自动化验收

按顺序运行：

```powershell
powershell -File Scripts/BuildEditor.ps1
powershell -File Scripts/ConfigurePhysicsWorldBoxRigidBody.ps1
powershell -File Scripts/ValidatePhysicsWorldBoxPhysicsPIE.ps1
powershell -File Scripts/ValidatePhysicsWorldMeleeBoxPIE.ps1
powershell -File Scripts/ValidatePhysicsWorldP0PIE.ps1
powershell -File Scripts/ValidateRoverPIE.ps1
```

`ConfigurePhysicsWorldBoxRigidBody.ps1` 只更新木箱刚体、木箱材质 Usage、质量和世界重力，不覆盖已经手调的战斗参数。完整重建 P0 占位资产时才运行 `ConfigurePhysicsWorldP0.ps1`。

调参入口：

- 单个关卡木箱：选中 Actor，在 `World Interaction|Physics` 中调整 `BoxMassKg`；设为 `0` 时使用共享默认质量；
- 共享默认质量：`DA_WorldInteractionConfig -> Settings -> Destructible|Physics -> DestructibleBoxDefaultMassKg`；
- 项目 Demo 统一重力：`DA_WorldInteractionConfig -> Settings -> Physics|World -> WorldGravityZ`，由 `bOverrideWorldGravity` 控制是否覆盖 WorldSettings；
- 统一重力会同时影响角色、ProjectileMovement、普通刚体和 Chaos 碎片，不提供木箱私有的重力倍率。

专项 PIE 必须同时证明：

- Rover 拥有 `URoverWorldSkillComponent`，火球请求能生成真实投射物；
- PIE World 自动创建 `UWorldInteractionSubsystem`；
- 火球命中木箱后只产生一次 Explosion 请求，表面解析为 Wood；
- 至少一个 `UPhysicsInteractable` 接收请求，木箱生命降至 0 并激活 Geometry Collection；
- 完整箱开启刚体与重力，逐实例质量可运行时修改；破裂前后总质量一致，Transform 连续；
- 爆炸/表面 Niagara 与灼烧贴花配置有效，活动贴花数不超过 Config 上限；
- 已引爆投射物在下一帧被清理，木箱碎片按 Config 生命周期回收；
- 原有 Rover 移动、输入映射和跳跃冒烟不回归。

### 8.2 性能证据模板

P0 只建立采样方法，不凭占位场景宣称已完成优化。正式记录至少包含：

| 条件/指标 | 要求 |
|---|---|
| 测试环境 | CPU、GPU、内存、分辨率、画质、Development/Shipping、编辑器或独立进程 |
| 路线 | 固定相机路线和 30 秒样本；冷启动与热缓存分开 |
| 负载 | 连续火球、同时活动贴花、Chaos 碎片、Niagara 系统数均记录峰值 |
| 帧数据 | Frame/Game/Render/GPU 的 p50、p95 和峰值，不只截一帧 `stat unit` |
| 生命周期 | 投射物、贴花、碎片在配置时限后无持续增长 |
| 预算 | 目标帧率与各线程预算在首次基准采样后填写 `[PLACEHOLDER]`，不得倒推结果 |

P3 的 World Partition/HLOD 验收必须额外记录测试区域尺寸、加载半径、网格数量、Actor/HISM 实例数、内存和流送尖峰；这些证据不能由当前营地关卡替代。
