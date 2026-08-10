# 大世界物理交互 Demo 开发规划

> 版本：v0.8.0
> 日期：2026-08-09
> 定位：大世界表现与交互 / 技术美术（物理方向）作品集垂直切片

## 1. 目标变更

项目不再以“完整复刻《鸣潮》漂泊者玩法”为最终目标。现有漂泊者移动、相机、四段轻击、重击 / 鸣奏、空中攻击、武器、命中与受击系统作为稳定的玩家交互底座，后续主线改为：

> 在 UE 5.8 中制作一个可演示、可度量、可扩展的开放区域物理交互 Demo，展示战斗输入如何统一驱动 Chaos、Niagara、材质、植被、水体与大世界加载系统。

最终交付包括：

- 一套可扩展的环境交互框架；
- 一个废弃营地主题的可玩垂直切片；
- 一条完整的“技能命中 → 环境物理响应 → 视觉反馈”展示链路；
- 性能与精度取舍数据；
- 约 3 分钟演示视频、README 和面试讲解材料。

## 2. 体验设计支柱

本项目不以“接入了多少 UE 功能”为完成标准，而以玩家是否能从环境反馈中读懂规则为标准：

1. **输入一致**：近战、投射物、移动、起跳和落地都使用玩家已经掌握的操作，不为物理演示增加孤立按键；
2. **反馈有层级**：单点命中强调方向与时机，范围爆炸强调影响半径，动态路径强调重量、速度和余振；
3. **环境状态连续**：落叶、纸片等环境元素先于输入存在，玩家扰动已有状态，而不是每次操作重新生成一份特效；
4. **结果可解释**：武器碰撞、表面类型、作用范围、冲量来源和生命周期均可观察、可调、可回归；
5. **动态环境可通行**：会晃、会碎不等于体验成立，桥不能塌、碎片不能无限累积、反馈不能抢走玩家控制权；
6. **规模化有预算**：当前营地只验证交互语言，后续大世界阶段必须补充加载、实例数量、帧时间和内存证据。

## 3. 范围判断

### 3.1 保留

- `ARoverCharacter`、`URoverLocomotionComponent`、`URoverCombatComponent` 与现有动画资产；
- 地面四段轻击循环、上下文重击 / 鸣奏、空中攻击、武器 Trace、伤害和受击反馈；
- 每段方向选择、Tap / Hold、ComboWindow / ResonanceWindow、输入缓冲、RequestId 握手和 Watchdog；
- C++ 核心逻辑 + 蓝图/AnimBP/Niagara 表现的混合架构；
- 自动化资产生成、BuildEditor 与无头 PIE 验证流程。

### 3.2 暂停

- 完整角色复刻、第五段以上轻击、空中多段连招、余音、闪避、攀爬、钩锁等角色系统；
- 与物理交互作品集没有直接展示价值的动画手感扩展；
- 在核心闭环完成前堆叠水体、布料、PCG、HLOD 等孤立功能。

### 3.3 不采用

- 关卡中放置并全局查找 `BP_InteractionManager` 的伪单例；
- 让角色、技能、木箱、贴花和 Niagara 互相直接引用；
- 用 100m × 100m 场景本身作为“大世界”证明；
- 只有视频效果、没有参数来源和性能数据的不可复现实现。

## 4. 交互规则与实现边界

```text
Rover Character / Combat
        │ 标准化 Hit / Skill 请求
        ▼
UWorldInteractionSubsystem
        │
        ├── Surface Resolver（物理材质 / Surface Type）
        ├── Physics Response（Chaos 刚体 / External Strain / Geometry Collection）
        ├── VFX Response（Niagara）
        ├── Material Response（当前 Decal；MPC / WPO 计划）
        └── World Response（Wind / Water / Foliage）
                    │
                    ▼
        UPhysicsInteractable 实现对象
```

### 4.1 Runtime 类型

- `EWorldElementType`：Physical、Fire、Wind、Water 等交互元素；
- `FWorldInteractionRequest`：来源、命中结果、元素、伤害、冲量、半径、标签；
- `UPhysicsInteractable`：环境对象接收请求的统一接口；
- `UWorldInteractionSubsystem`：当前 World 内的交互路由、全局参数和调试统计；
- `UWorldInteractionConfig`：表面映射、爆炸衰减、贴花生命周期、风力等数据。

### 4.2 边界

- 角色与战斗组件只提交请求，不知道木箱、篝火或贴花 Actor；
- 可交互 Actor 决定自身状态变化，Subsystem 负责共享反馈和调度；
- 当前 Niagara、材质和 Chaos 数值通过 DataAsset 与 Niagara User 参数注入；MPC / WPO 属于后续草地与全局风阶段；
- 所有新资产放入 `Content/PhysicsWorldDemo/`，与 `Content/Rover/` 分离。

## 5. 分阶段路线

### P0：交互闭环

目标：证明现有战斗底座可以稳定驱动环境反馈。

1. 创建交互枚举、请求结构、接口、Subsystem 与 Config；
2. 添加火球技能输入和投射物，命中后产生标准化请求；
3. 创建可破坏木箱：生命值归零后从完整刚体单阶段切换到 Geometry Collection，并触发真实破裂木屑；
4. 建立 Surface Type 映射框架并完成 Wood 链路，其他表面差异化表现转入 P1；
5. 添加爆炸 Niagara、Chaos 径向冲击和灼烧贴花；
6. 建立专项 PIE：验证请求、表面分流、伤害、破坏、贴花生命周期和清理。

当前玩家输入载体已经扩展到四段定向轻击、普通重击、重击·鸣奏、Attack03 飞剑与单段空中攻击。它们用于提供方向、强度、持续时间和垂直冲击四类环境测试向量；完成这些输入后不再继续无边界扩展角色战斗内容。

当前验收场景：玩家发射火球命中木箱，木箱受伤/破碎，同时产生木屑、爆炸冲击和灼烧痕迹。Stone / Metal / Grass 已保留协议槽位，但不把尚未完成的视觉差异写成 P0 成果。

### P0.5：动态路径原型（反馈分层完成，视觉与压力测试待验收）

目标：让玩家通过站立、行走、跑步、起跳、落地和离开，读懂一条物理吊桥的承重、惯性与恢复过程。

当前结构已完成：

- 已记录演示实例为 60 块独立刚体木板，桥面约 `16.77m x 4.00m`；
- 两端四个桥墩 / 挂点；
- 每条板缝左右各一组约束，共 `122` 个 Constraint；
- 双侧挂点用几何力臂抑制宽木板横滚，保留沿桥方向的自然俯仰；
- 角色离开后延迟恢复自然弧线，满足角度与速度容差后进入 Chaos Sleep；
- 起跳和落地冲量分摊到中心板及相邻板，减少单板弹飞。

攻击与落地反馈分层已完成：

- 动态底座攻击使用平滑 `ConstantForce`，保留 `0.55` 距离、`2.50` 时长缩放和 `1.0` Ease；角色相对脚下板前移 `18.7cm`；
- 攻击期间 CharacterMovement Push Force、世界交互请求和额外桥体 Movement Impulse 均为 `0`，角色站立载荷保持完整 `1.0`；
- 桥板 DirectHit 由桥自行按 `0.05` 倍、最大 `50` 消费；木箱通用冲量和 Explosion 径向冲量不受影响；
- 桥只在攻击推进及其 `1.0s` 余量窗口使用 `4x / 6x` 线性与角向阻尼，结束后恢复基础阻尼；
- 最新专项连续执行两次 Attack01，最大峰值 `158.8cm/s / 193.3deg/s`；Landing 为 `309.2cm/s`，线速度比 `0.514`；
- 自动化已固化 Attack / Landing `<=0.75`、攻击角速度 `<=280deg/s`、相对推进 `>=15cm`、战斗阻尼启用 / 恢复和完整站立载荷门槛。

当前剩余项是有渲染手感、真实刀刃 DirectHit 行为、Explosion 稳定性和多桥性能，不再把“攻击高于落地”列为当前问题。

### P0.75：常驻地表轻质杂物（规则完成，视觉与性能待验收）

目标：让落叶和纸片成为持续存在的环境状态，并用同一批粒子反馈玩家移动、攻击、起跳、落地和爆炸。

当前规则已完成：

- 一个世界锚定 Region 维持约 `450` 个 Ambient 粒子的稳态预算；
- 交互只扰动环境中已有粒子，不为移动、攻击、落地或爆炸重新生成 Niagara System；
- 移动、Attack、Jump、Landing、Explosion 使用独立来源类型和每帧预算；
- Attack 使用地下 / 后方排斥力解除贴地状态，再用前上方吸引力形成沿刀路的短尾流；
- Point Force 原点限制在自身半径的 `0.8R` 内，避免强上抬把力源移出覆盖范围；
- Ambient 粒子落地后保持可再次受力；源码与已重建资产将持续旋转驱动力设为 `0/0`、Rotational Drag 设为 `8`、Restitution 设为 `0`，无头参数验证已通过，最终观感仍待有渲染 PIE；
- 世界空间 CPU Emitter 已由 Fixed Bounds 改为 Dynamic Bounds，解决镜头向下时整套 Renderer 被视锥裁剪的问题；`Pitch=-45deg` 预览保持可见；
- 轻量场不进入标准 Chaos 请求链，不改变木箱、吊桥或角色的真实物理冲量；
- DataAsset 增加一次性 Schema 迁移，Niagara 模块 / User 参数绑定失败会让资产生成流程失败；
- 无头 PIE 覆盖静止、走跑、空挥、起跳、落地、爆炸、攻击尾流、空间覆盖、限流与 `interaction_systems=0`。

当前仍未完成：

- 有渲染 PIE 下静止 `15s`、落地再激活和持续自转的最终观感确认；
- 走 / 跑 / 起跳 / 落地 / 攻击 / 爆炸的固定路线视频与参数快照；
- Niagara CPU Sim 单区域的固定硬件 p50 / p95 基线；
- CPU Ambient 与 GPU + Niagara Data Channel 消费的同场景 A/B；
- 多 Region、World Partition Cell、区域边缘、LWC 远原点以及 Dynamic Bounds 成本验证。

### P1：表面语言与环境表现

目标：展示统一环境参数影响不同系统。

1. 完成 Wood / Stone / Metal 的差异化命中、破坏、声音和残留痕迹；
2. 固化常驻轻质杂物的视觉基线与平台预算；
3. 篝火：火焰、烟雾、余烬、动态光；
4. 全局风：统一驱动 Niagara、旗帜、营帐和植被材质；
5. Chaos Cloth 旗帜与营帐，补充碰撞和极端参数保护；
6. 调试面板显示风向、风强、活跃粒子和活动物理对象数量。

### P2：地表与生态交互

目标：加入角色和环境的持续接触反馈。

1. 草地 WPO 弯曲与恢复；
2. Water Plugin / WaterAdvanced 水体：双湖走跑、跳跃、落地、攻击与爆炸 P0 已完成；游泳、浮力、湿身和元素反应待开发；
3. 火、风、水之间最少一组组合反应；
4. 统一调试可视化与对象池策略。

### P3：PCG 与大世界证据

目标：用数据证明可扩展性，不只展示一块小营地。

1. PCG 生成树林、草地和石头，并支持营地排除区；
2. 在首轮流送基线后确定扩展测试区域尺寸，并启用 World Partition / Data Layer；
3. 配置 HLOD、加载距离与实例化策略；
4. 记录不同视距下 Actor/实例数量、Game/GPU 帧时间、内存与 Chaos/Niagara 开销；
5. 给出质量优先与性能优先两套配置对比。

### P4：作品集交付

1. 完成场景光照、镜头路线和交互引导；
2. 制作 3 分钟演示：目标、架构、交互闭环、性能对比、工具与管线；
3. README 记录架构图、资产管线、关键参数和复现步骤；
4. 整理每个模块的原理、取舍、故障案例与优化结果。

## 6. 可行性与体验风险

| 项目 | 结论 | 主要风险 |
|---|---|---|
| Niagara 火焰/爆炸 | 可行 | 高质量材质与贴图仍需美术资产和人工调参 |
| Niagara 常驻地表杂物 | P0 规则已实现 | 当前 CPU Sim 尚未完成 GPU 扩量、多区域与固定硬件性能证据 |
| Chaos Destruction | 可行 | Fracture、碰撞层级和碎片数量直接影响稳定性与性能 |
| Chaos Cloth | 可行 | Cloth Asset、权重绘制、碰撞体通常需要编辑器内人工处理 |
| 表面/贴花反馈 | 高可行 | 需要统一 Physical Material 资产规范与对象池 |
| 草地 WPO | 高可行 | 大范围 MPC/RVT 写入方式需评估精度与成本 |
| Water | 双湖浅水交互 P0 已通过 | WaterAdvanced 为 Experimental；远距离多水域、游泳、浮力与正式 GPU 基线待验证 |
| PCG | 可行 | 必须准备可用植被资产、碰撞和 Nanite/HISM 策略 |
| World Partition/HLOD | 可行 | 100m 场景无法形成有说服力的流送与 HLOD 数据 |
| 长链动态吊桥 | 结构与攻击 / 落地分层已通过 | 真实刀刃 DirectHit、Explosion、多桥性能与最终美术仍待验收 |

原文“约 11 天完成全部系统”只适合功能原型，不适合作品集质量承诺。正式排期应以 P0 闭环通过自动化验证后，再根据美术资产准备情况评估。

## 7. 当前可玩状态

本机 UE 5.8 插件基线：Niagara 与 PCG 为默认启用；工程已显式启用 Geometry Collection、Water 与 WaterAdvanced，WaterAdvanced 仍标记为 Experimental；Chaos Cloth 随后续阶段开启。

当前已按以下顺序完成 P0，不先做布料：

1. 已启用并验证 P0 所需插件/模块；
2. 已建立 `UWorldInteractionSubsystem`、请求结构、接口与 DataAsset；
3. 已把现有武器 Trace 的非角色命中转换为交互请求；
4. 已实现 `Q` / 手柄右肩键火球、投射物与爆炸请求；
5. 已创建开启刚体模拟的可破坏木箱、16 碎片单根簇 Geometry Collection、Physical Material、Niagara 和灼烧贴花；
6. 完整箱与破裂 Geometry Collection 共用逐实例质量语义，世界重力由 `DA_WorldInteractionConfig` 统一下发；
7. 已增加幂等资产生成、Build 和专项 PIE 验证脚本。
8. 已将两个现有 WaterBodyLake 接入 WaterAdvanced Grid2D 浅水模拟，完成走跑、跳跃、落地、攻击和爆炸反馈，并验证 14 Bodies 角色连续 Collider 与跨 Lake 过滤。

当前 Geometry Collection 已完成结构与运行时验证；木箱 / 火球链保留 4 套 Niagara 反馈资产，并由组件级 `OnChaosBreakEvent` 驱动真实破裂木屑。Loose Debris 另有 1 套当前活动的 Ambient 常驻系统；Movement / Attack / Landing / Explosion 模板资产仍保留，但运行时不生成。完整箱与碎片具有统一质量语义；2026-08-09 木箱专项重新确认 `80kg`、`-980cm/s²` 和质量相关破裂冲量，但仍包含表现调参，不宣称已经完成严格物理标定。Niagara、木箱材质和灼烧贴花仍处于 P0 视觉质量，不作为最终美术质量交付。

当前玩家可体验四类环境语言：

| 玩家行为 | 环境回应 | 当前状态 |
|---|---|---|
| 左键近战命中木箱 | 第一刀立即破裂，显示武器 Sweep 和定向碎片反馈 | 已完成 |
| `Q` 发射火球 | 范围内多个木箱响应，叠加爆炸、木屑和灼烧痕迹 | 已完成 |
| 站立、走跑、跳跃、攻击通过吊桥 | 桥面承重、摆动、衰减并恢复自然弧线；攻击保留位移且低于落地强度 | 结构与自动层级验收完成 |
| 静止、走跑、跳跃、攻击、爆炸经过杂物区 | 同一批落叶 / 纸片被扰动，落地后可再次唤醒，攻击沿刀路形成短尾流 | 规则完成，最终视觉与性能验收中 |

下一步顺序调整为：先完成轻质杂物与吊桥的有渲染观感确认；为吊桥补真实刀刃 DirectHit、Explosion 压力和多桥性能测试；随后固化 Niagara 与 Chaos 的固定硬件性能基线，再打磨 Wood / Stone / Metal 差异化反馈，之后才进入全局风和 Chaos Cloth。

## 8. 对原始需求文档的修订结论

| 原始方案问题 | 改进方式 |
|---|---|
| 以功能清单和约 11 天排期驱动，核心链路可能最后才连通 | 先完成可玩的 P0 垂直切片，再按 P1-P4 扩展 |
| 关卡内 `BP_InteractionManager` / `BP_ImpactManager` 形成查找式伪单例 | 使用随 World 创建和销毁的 `UWorldInteractionSubsystem` |
| 技能、木箱、Niagara、贴花彼此直接引用 | 用 `FWorldInteractionRequest` 和 `UPhysicsInteractable` 隔离生产者与响应者 |
| 缺少请求身份、非法输入保护、生命周期与数量上限 | 请求使用 `FGuid` 做身份追踪；校验半径/伤害/冲量；投射物、碎片和贴花均有清理策略。当前不宣称 Subsystem 已按 `FGuid` 幂等去重 |
| 验收项多为“看起来有效”，无法回归 | 增加资产生成脚本、专项 PIE、统计计数和明确成功标记 |
| 100m × 100m 营地直接被称为大世界 | 营地只证明交互闭环；World Partition/HLOD 必须在 P3 扩区并提交测量数据 |
| 性能描述只有 `stat unit` 等命令，没有测试条件 | 固定硬件、构建配置、相机路线、样本时长和 p50/p95 指标后再下结论 |

## 9. 验收与证据

### 9.1 自动化验收

按顺序运行：

```powershell
powershell -File Scripts/BuildEditor.ps1 -EngineRoot <UE5.8-path>
powershell -File Scripts/ConfigurePhysicsWorldBoxRigidBody.ps1 -EngineRoot <UE5.8-path>
powershell -File Scripts/ValidatePhysicsWorldBoxPhysicsPIE.ps1 -EngineRoot <UE5.8-path>
powershell -File Scripts/ValidatePhysicsWorldMeleeBoxPIE.ps1 -EngineRoot <UE5.8-path>
powershell -File Scripts/ValidatePhysicsWorldP0PIE.ps1 -EngineRoot <UE5.8-path>
powershell -File Scripts/ValidatePhysicsWorldRopeBridgePIE.ps1 -EngineRoot <UE5.8-path>
powershell -File Scripts/ConfigurePhysicsWorldLooseDebris.ps1 -EngineRoot <UE5.8-path>
powershell -File Scripts/ValidatePhysicsWorldLooseDebrisPIE.ps1 -EngineRoot <UE5.8-path>
powershell -File Scripts/ValidateRoverPIE.ps1 -EngineRoot <UE5.8-path>
```

`ConfigurePhysicsWorldBoxRigidBody.ps1` 只更新木箱刚体、木箱材质 Usage、质量和世界重力，不覆盖已经手调的战斗参数。完整重建 P0 生成资产时才运行 `ConfigurePhysicsWorldP0.ps1`。

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

吊桥专项验收必须同时证明：

- 60 板生成 `122` 个 Constraint，其中 59 条板缝均存在左右两组约束；
- 四个端点 Frame 和板间距离误差不超过约定阈值，不出现 NaN、断裂或持续能量增长；
- 站立、走、跑、起跳、落地使用彼此可比较的桥体响应指标，而不是只比较输入冲量；
- 攻击期间世界交互请求、动画 Root Motion、CharacterMovement Push Force 和额外桥体 Movement Impulse 均应为零；
- 攻击推进必须按配置保留并沿攻击方向可观测；当前动态底座基线为 `0.55` 距离、`2.50` 时长和 `1.0` Ease，相对脚下板前移 `18.7cm`，自动化最低门槛为 `15cm`；
- 两次 Attack01 取最大值后，攻击 / 落地线速度比不得高于 `0.75`，攻击角速度不得高于 `280deg/s`；
- 攻击站立载荷必须保持完整 `1.0`，桥侧战斗阻尼必须在推进时启用并在 `1.0s` 余量后恢复；
- DirectHit 参数和接收者自处理链必须有效；真实刀刃命中桥板的最终冲量断言仍是下一项专项；
- 离桥 10 秒后桥体速度归零、自然站姿误差不超过 `2deg`，并退出恢复驱动进入 Sleep。

轻质杂物专项验收必须同时证明：

- 静止不持续发布 Movement Field，移动、空挥、起跳、落地与爆炸来源均可观察；
- Attack Wake 目标位于真实刀路前上方，而不是角色固定朝向；
- Point Force 地下偏移仍处于作用半径和 `0.8R` 上限内；
- 每来源限流和每帧预算有效，丢弃计数可观察；
- 轻量场不会增加标准 Chaos Processed Request；
- 交互不创建额外 Niagara System，`interaction_systems=0`；
- 配置资产完成当前 Schema 迁移，模块 / User 参数绑定失败不能输出成功；
- 有渲染 PIE 额外证明静止 `15s` 后无持续自转，贴地粒子能被下一次交互重新唤醒。

### 9.2 性能证据模板

P0 只建立采样方法，不凭占位场景宣称已完成优化。正式记录至少包含：

| 条件/指标 | 要求 |
|---|---|
| 测试环境 | CPU、GPU、内存、分辨率、画质、Development/Shipping、编辑器或独立进程 |
| 路线 | 固定相机路线和 30 秒样本；冷启动与热缓存分开 |
| 负载 | 连续火球、同时活动贴花、Chaos 碎片、Niagara 系统数均记录峰值 |
| 帧数据 | Frame/Game/Render/GPU 的 p50、p95 和峰值，不只截一帧 `stat unit` |
| 生命周期 | 投射物、贴花、碎片在配置时限后无持续增长 |
| 预算 | 目标帧率与各线程预算在首次固定硬件基准采样后固化，不得倒推结果 |

P3 的 World Partition/HLOD 验收必须额外记录测试区域尺寸、加载半径、网格数量、Actor/HISM 实例数、内存和流送尖峰；这些证据不能由当前营地关卡替代。
