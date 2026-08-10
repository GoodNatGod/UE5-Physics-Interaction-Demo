# 攻击连招修正 — Codex 紧急修复指令

> **版本**: 1.1.0  
> **目标**: 修复三段攻击连招的四个核心 Bug  
> **前置文档**: `闪避与收招系统实现规格.md` (v0.2.2) §5-6  
> **适用引擎**: Unreal Engine 5.8

---

## 现状问题（必须逐条修复，不要跳过任何一条）

| # | 问题 | 严重度 |
|---|------|:------:|
| 1 | 攻击时模型滑行一段后弹回原位 | **致命** |
| 2 | 连按攻击键无法触发连招（L1→L2→L3 不通） | **致命** |
| 3 | 收招动画完全播完才能进行下一次攻击 | **致命** |
| 4 | 整体手感僵硬，不像动作游戏 | 前三条的连锁反应 |

---

## 修复 1：Root Motion — 禁止攻击动画移动胶囊

### 问题根因

攻击动画播放 Montage 时，动画内烘焙的 Root Motion 数据移动了角色胶囊，但 `LocomotionComponent` 仍在每帧用移动逻辑修正位置 → **滑行后弹回**。

### 修法

攻击动画全部使用 **In-Place 模式**：Root Motion 数据驱动骨骼姿势，不驱动胶囊位移。

**步骤 1：修改动画资产**

打开 `Attack01`、`Attack02`、`Attack03` 的 Animation Sequence 资产，在 Asset Details 中设置：

```
Enable Root Motion         = false            ← ★ 关键
Root Motion Root Lock      = RefPose          （已有配置，不动）
Force Root Lock            = true             （如果 FBX 自带 Root Motion 且 UE 侧无法覆盖）
```

**步骤 2：确认 Montage 没有额外启用 Root Motion**

打开每个 Attack Montage 资产（`AM_Attack01`/`02`/`03`），检查 `Montage` 分组的 `Root Motion` 相关设置：

```
Enable Auto Blend Out      = true             ← 攻击结束后自动过渡
Root Motion Scale          = 1.0
```

不要在这里额外勾选任何 Root Motion 选项——Montage 继承 Animation Sequence 的设置。

**步骤 3：重新导入时强制关闭 Root Motion（如果步骤 1 无效）**

如果 FBX 源文件自带 Root Motion 数据且步骤 1 无法在 UE 侧覆盖，重新导入时：

```
Import Settings:
  Mesh → Import Meshes in Bone Hierarchy = true
  Animation → Import Animation            = true
  Transform → Import Translation          = false   ← ★ 丢弃源文件位移数据
  Transform → Import Rotation             = true
```

**验证标准**：攻击 Animation Sequence 不得向胶囊输出动画 Root Motion。在本项目中，胶囊仍可由 `URoverLocomotionComponent` 的攻击推进 Root Motion Source 受控前移；这条路径与动画 Root Motion 必须保持互斥。在 PIE 中按 `~` 打开控制台，输入 `ShowDebug Movement`，确认位移只来自攻击推进请求，不会滑行后弹回。

---

## 修复 2：ComboWindow NotifyState — 确保连招窗口正确开启

### 问题根因

Attack01~03 必须使用一个真正的 `AnimNotifyState` 表示连招窗口。若仍使用分离的 `RoverComboWindowBegin`/`End` Notify，或者窗口没有延伸到 Montage 末尾，运行时就无法稳定区分“窗口前缓冲”和“窗口内立即切段”。

### 修法：每个 Attack Montage 的 Notify 顺序

打开每个 Attack Montage（`AM_Rover_Attack01`/`02`/`03`），确认以下 Notify 与 `ComboWindow` NotifyState 存在且归一化时间正确：

```
归一化时间    Notify / NotifyState          作用
────────────────────────────────────────────────────
[0.00]       RoverAttackStarted            攻击确认开始，显示武器
[逐段配置]   RoverAttackActiveBegin/End     开启/关闭武器碰撞 Trace
[0.50,1.00]  ComboWindow (NotifyState)      窗口内输入立即切下一段
[0.75]       RoverRecoveryBegin             收招尾段开始
[~1.00]      RoverAttackFinished            攻击即将结束
```

**四条硬约束**：

1. `ComboWindow` 起点默认是每段动画的 `0.50`，这是 `[PLACEHOLDER]`，允许逐段数据化微调。
2. `ComboWindow` 终点必须等于 Montage 末尾，因此它可以覆盖 Recovery。
3. 窗口内按 Attack 必须同调用栈切到下一段，不等待 NotifyState End。
4. 窗口前的输入只在 `0.25s` 内有效；超时必须清除，不能误触发下一段。

**验证**：PIE 中在 `URoverCombatComponent::OnComboWindowBegin` 回调里加日志打印：

```
LogTemp: ComboWindow=true (ComboIndex=1)
```

如果从未出现这行日志 → Notify 缺失或没被 AnimBP 触发。

---

## 修复 3：ComboComponent 的输入处理 — 正确实现连招缓存

### 问题根因

Codex 的 `HandleAttackInput()` 大概率只在 `CombatPhase == None` 时才处理输入，其他阶段直接 return false。或 `CurrentComboIndex` 在 Recovery 开始时就被清零。

### 修法：URoverCombatComponent 的完整输入状态机

```cpp
bool URoverCombatComponent::RequestLightAttack()
{
    if (CombatPhase == ERoverCombatPhase::None)
    {
        return StartAttackSegment(NextComboIndex);
    }

    if (bComboWindowOpen)
    {
        bBufferedAttackInput = false;
        AttackInputBufferRemaining = 0.0f;
        return TransitionToNextAttack();
    }

    bBufferedAttackInput = true;
    AttackInputBufferRemaining = 0.25f;
    return true;
}
```

### ComboWindowBegin 回调

```cpp
void URoverCombatComponent::BeginComboWindow(int32 RequestId)
{
    if (RequestId != ActiveAttackRequestId)
    {
        return;
    }
    bComboWindowOpen = true;
    if (bBufferedAttackInput && AttackInputBufferRemaining > 0.0f)
    {
        bBufferedAttackInput = false;
        AttackInputBufferRemaining = 0.0f;
        TransitionToNextAttack();
    }
}
```

`ComboWindow` 的 End 只关闭 `bComboWindowOpen`。下一段切换已经在“窗口内按键”或 NotifyState Begin 消费缓冲时完成。

### TransitionToNextCombo 实现

```cpp
void URoverCombatComponent::TransitionToNextCombo()
{
    int32 NextIndex = (PendingComboInput) ? PendingComboTargetIndex : CurrentComboIndex + 1;

    // 超出 Combo 段数 → 回到 L1
    if (NextIndex >= AttackDefinitions.Num())
    {
        NextIndex = 0;
    }

    // 停止当前 Montage，立即播下一段
    AnimInstance->Montage_Stop(0.0f, CurrentAttackMontage);
    CurrentComboIndex = NextIndex;
    CombatPhase = ERoverCombatPhase::Startup;
    PlayAttackMontage(NextIndex);
}
```

### 核心要点

- **ComboWindow 内按攻击立即切动画** — 同帧停止当前 Montage 并播放下一段
- **输入缓冲 0.25s** — 在 ComboWindow 之前按下攻击，窗口开启时只消费仍有效的输入
- **Recovery 与 ComboWindow 可重叠** — 只要窗口仍开启，Recovery 中输入也立即续段
- **Montage 结束后保留索引 0.55s** — 计时内继续连击，超时才将 `CurrentComboIndex` 置为 `-1`

---

## 修复 4：Montage Blend Out — 让过渡不抖

### 问题根因

Montage 之间的切换没有过渡时间 → 首帧姿势突变 → 僵硬。

### 修法

每个 Attack Montage 资产设置：

```
Blend In:
  Blend Time                = 0.05s           ← ★ 快速但非零，消除首帧 pop
  Blend Mode                = BlendByTime

Blend Out:
  Blend Time                = 0.10s           ← ★ 连招切段时用这个时间过渡
  Blend Mode                = BlendByTime
  Blend Out Trigger Time    = 0.10s           ← UE 属性单位是“距结尾剩余秒数”，不是归一化进度
```

当前攻击 Montage 长度下，`0.10s` 对应的归一化开始点均大于 `0.80`。不要把 `0.85` 直接写入该属性，否则长动画会在 Recovery/Finished Notify 前提前结束。

同时在 AnimBP 的 Slot 节点后面加一个 **Inertialization** 节点（UE5 内置），Montage 切换时骨骼自动插值，不需要手写衔接动画。

### AnimBP 修改（如果还没有）

```
AnimGraph 的 DefaultSlot → 加一个节点:
  "Inertialization" (在 Animation 分类下)
  → 连接到 Output Pose
```

这个节点会在 Montage 切换时自动计算前后两帧的骨骼差并插值，消除切换时的瞬跳。

---

## 修复 5（如果以上全修完还是硬）：Hit Stop + 动画速率

### 5.1 Hit Stop（命中顿帧）

没有 Hit Stop 的动作游戏 = 砍空气。实现方式：

```cpp
void URoverCombatComponent::OnWeaponHit(AActor* HitTarget)
{
    // 全局时间减速 → 命中瞬间的"打击感"
    UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 0.1f);

    FTimerHandle HitStopTimer;
    GetWorld()->GetTimerManager().SetTimer(HitStopTimer, [this]()
    {
        UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 1.0f);
    }, 0.05f, false);  // [PLACEHOLDER: 0.03~0.08s]
}
```

### 5.2 动画速率（Anim Rate Scale）

如果动画本身帧数太长导致节奏慢，可以在 AttackDefinitions 中加一个字段：

```cpp
UPROPERTY(EditDefaultsOnly, Category = "Attack")
float AnimPlayRate = 1.0f;  // [PLACEHOLDER: 1.0~1.3]
```

播放 Montage 时传入：

```cpp
AnimInstance->Montage_Play(CurrentAttackMontage, AttackDef.AnimPlayRate);
```

**调整优先级**：先调完 Combo 逻辑（修复 1-4）再调速率。速率是 polish，不是修复。

---

## 一次性检查清单（Codex 必须逐项打勾）

在提交修复前，逐项确认：

- [ ] Attack01/02/03 的 Animation Sequence 设了 `EnableRootMotion = false`
- [ ] 播放 Attack01 全程，角色世界坐标不变（< 2cm 浮点误差）
- [ ] 每个 Attack Montage 恰好有一个 `ComboWindow` AnimNotifyState，默认 `[0.50, 1.00]`
- [ ] 窗口内按 Attack 立即停旧播新，不等待窗口结束
- [ ] 窗口前输入进入 `0.25s` 缓冲，窗口开启时立即消费，超时后不会误跳段
- [ ] Montage 正常完全结束后启动 `0.55s` 续段计时，计时内输入继续当前连击
- [ ] 续段计时超时后 `CurrentComboIndex == -1`，下一次从 Attack01 开始
- [ ] Dodge 中断会立即停止攻击并清除 Combo、武器、Trace、攻击推进和移动限制
- [ ] 三个独立 Montage 用 `Montage_Stop(0.0f)` + 立即 `Montage_Play` 实现同帧切段
- [ ] 每个 Montage 的 Blend Out 归一化开始点 `≥ 0.80`（资产属性本身填写剩余秒数，当前为 `0.10s`）
- [ ] AnimBP 的 Slot 节点后有 Inertialization 节点
- [ ] PIE 测试：连按三次攻击 → L1→L2→L3 连续播放，中间无 Idle 帧
- [ ] PIE 测试：只按一次攻击 → L1 完整播完 Recovery → 回 Idle
- [ ] PIE 测试：L1 ComboWindow 内按攻击 → 进入 L2，不播 L1 的 Recovery
- [ ] PIE 测试：Montage 结束后 `0.55s` 内输入 → 进入下一段；超时后输入 → Attack01

---

## 参考：CombatPhase 状态机流转图

```
None
  │
  ↓ 按攻击键 (ComboIndex=0)
Startup ────────────────→ RoverAttackStarted Notify
  │
  ↓ RoverAttackActiveBegin Notify
Active ─────────────────→ 武器碰撞 Trace 开启
  │
  ↓ ComboWindow NotifyState Begin (默认 50%)
Window Open ────────────→ ★ 按攻击或消费有效缓冲 → 立即 Startup(下一段)
  │
  ↓ RoverRecoveryBegin（窗口仍可保持开启到 Montage 末尾）
Recovery ───────────────→ 窗口内输入仍立即切下一段
  │
  ↓ Montage 完全结束
None + 0.55s Timer ─────→ 计时内输入继续；超时后 ComboIndex=-1
```

---

## 如果修复后仍"不像动作游戏"

问题出在动画资产本身，不是代码。三条底线检查：

1. **三段动画的 Pose 能接上吗？**
   - Attack01 最后一帧的右手/武器位置 ≈ Attack02 第一帧的位置？
   - **不接 → 需要增大 Blend Time 到 0.15~0.2s，或手调衔接 Pose**

2. **每段动画的帧数合理吗？**
   - 30fps 动画：一段攻击 18~35 帧（0.6~1.2s）正常
   - 超过 50 帧 → 太慢，像回合制，需要调 `AnimPlayRate`

3. **等待帧（冻结帧）是不是太多了？**
   - 检查动画是否有长时间的"蓄力前摇"——动作游戏的前摇应该 < 0.2s
   - 如果素材的前摇很长，在 Montage 中把 `PlayRate` 提到 1.3~1.5
