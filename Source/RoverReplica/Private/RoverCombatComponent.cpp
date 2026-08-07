#include "RoverCombatComponent.h"

#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "RoverCharacter.h"
#include "WorldInteractionSubsystem.h"

// Debug visualization CVars.
// rover.combat.DrawAttackTrace 1 -> show the actual sphere sweeps.
// rover.combat.DrawHitReaction 1 -> show hit impact flash and direction.
#if WITH_EDITOR
static constexpr int32 DefaultDrawAttackTrace = 1;
#else
static constexpr int32 DefaultDrawAttackTrace = 0;
#endif

static TAutoConsoleVariable<int32> CVarDrawAttackTrace(
	TEXT("rover.combat.DrawAttackTrace"),
	DefaultDrawAttackTrace,
	TEXT("0=Off  1=Show every weapon sphere sweep capsule during the Active phase."),
	ECVF_Cheat);

static TAutoConsoleVariable<float> CVarDrawAttackTraceDuration(
	TEXT("rover.combat.DrawAttackTraceDuration"),
	0.35f,
	TEXT("Lifetime in seconds for rover.combat.DrawAttackTrace visualization."),
	ECVF_Cheat);

static TAutoConsoleVariable<int32> CVarDrawHitReaction(
	TEXT("rover.combat.DrawHitReaction"),
	0,
	TEXT("0=Off  1=Flash hit-impact sphere + direction arrow on hit reaction trigger."),
	ECVF_Cheat);

URoverCombatComponent::URoverCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void URoverCombatComponent::BeginPlay()
{
	Super::BeginPlay();
	CharacterOwner = Cast<ARoverCharacter>(GetOwner());
	if (!CombatConfig)
	{
		CombatConfig = LoadObject<URoverCombatConfig>(
			nullptr,
			TEXT("/Game/Rover/Combat/DA_RoverCombatConfig.DA_RoverCombatConfig"));
	}
	if (CharacterOwner && CharacterOwner->GetMesh())
	{
		AddTickPrerequisiteComponent(CharacterOwner->GetMesh());
	}
}

void URoverCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DisableWeaponTrace();
	if (CharacterOwner)
	{
		CharacterOwner->SetCombatWeaponVisible(false);
		CharacterOwner->CancelCombatAttackAdvance(ActiveAttackRequestId);
		CharacterOwner->EndCombatMovementRestriction(ActiveAttackRequestId);
	}
	Super::EndPlay(EndPlayReason);
}

void URoverCombatComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bBufferedAttackInput)
	{
		AttackInputBufferRemaining = FMath::Max(0.0f, AttackInputBufferRemaining - DeltaTime);
		if (AttackInputBufferRemaining <= 0.0f)
		{
			bBufferedAttackInput = false;
		}
	}

	if (CombatPhase != ERoverCombatPhase::None)
	{
		AttackWatchdogElapsed += DeltaTime;
		const float Timeout = bAttackAnimationAcknowledged
			? GetSettings().AttackActiveTimeout
			: GetSettings().AttackPendingTimeout;
		if (AttackWatchdogElapsed >= FMath::Max(0.05f, Timeout))
		{
			CancelAttack(ActiveAttackRequestId);
		}
	}
	else if (CurrentComboIndex >= 0 && ComboResetRemaining > 0.0f)
	{
		ComboResetRemaining = FMath::Max(0.0f, ComboResetRemaining - DeltaTime);
		if (ComboResetRemaining <= 0.0f)
		{
			ResetComboState();
		}
	}

	if (bInHitReaction)
	{
		HitReactionWatchdogElapsed += DeltaTime;
		if (HitReactionWatchdogElapsed >= FMath::Max(0.1f, GetSettings().HitReactionTimeout))
		{
			CancelHitReaction(HitReactionRequestId);
		}
	}

	if (bWeaponTraceActive)
	{
		PerformWeaponTrace();
	}
}

const FRoverCombatSettings& URoverCombatComponent::GetSettings() const
{
	return CombatConfig ? CombatConfig->Settings : FallbackSettings;
}

UAnimMontage* URoverCombatComponent::ResolveAttackMontage() const
{
	const FRoverAttackDefinition* Definition = GetAttackDefinition(CurrentComboIndex);
	return Definition ? Definition->Montage.LoadSynchronous() : nullptr;
}

float URoverCombatComponent::ResolveAttackPlayRate() const
{
	const FRoverAttackDefinition* Definition = GetAttackDefinition(CurrentComboIndex);
	return Definition ? FMath::Max(0.1f, Definition->AnimPlayRate) : 1.0f;
}

float URoverCombatComponent::ResolveAttackTransitionBlendOutTime() const
{
	const FRoverAttackDefinition* Definition = GetAttackDefinition(CurrentComboIndex);
	return Definition ? FMath::Max(0.0f, Definition->MontageBlendOutTime) : 0.0f;
}

UAnimMontage* URoverCombatComponent::ResolveHitReactionMontage() const
{
	const FRoverCombatSettings& Settings = GetSettings();
	return HitReactionType == ERoverHitReactionType::LightLeft
		? Settings.LightHitLeftMontage.LoadSynchronous()
		: Settings.LightHitRightMontage.LoadSynchronous();
}

bool URoverCombatComponent::RequestLightAttack()
{
	if (!CharacterOwner || bDead || bInHitReaction)
	{
		return false;
	}

	if (CombatPhase == ERoverCombatPhase::None)
	{
		const int32 AttackCount = GetLightAttackCount();
		const bool bContinueCombo = CurrentComboIndex >= 1 && ComboResetRemaining > 0.0f;
		const int32 NextComboIndex = bContinueCombo && AttackCount > 0
			? (CurrentComboIndex >= AttackCount ? 1 : CurrentComboIndex + 1)
			: 1;
		return StartAttackSegment(NextComboIndex);
	}

	if (CurrentComboIndex <= 0 || GetLightAttackCount() <= 0)
	{
		return false;
	}

	if (bComboWindowOpen)
	{
		bBufferedAttackInput = false;
		AttackInputBufferRemaining = 0.0f;
		return TransitionToNextAttack();
	}

	bBufferedAttackInput = true;
	AttackInputBufferRemaining = FMath::Max(0.0f, GetSettings().AttackInputBufferDuration);
	return true;
}

bool URoverCombatComponent::RequestDodgeInterrupt()
{
	if (bDead || bInHitReaction)
	{
		return false;
	}

	if (ActiveAttackRequestId > 0)
	{
		CancelAttack(ActiveAttackRequestId);
	}
	else
	{
		ResetComboState();
	}
	return true;
}

bool URoverCombatComponent::RequestRecoveryMovementInterrupt()
{
	if (bDead || bInHitReaction || CombatPhase != ERoverCombatPhase::Recovery || ActiveAttackRequestId <= 0)
	{
		return false;
	}

	const int32 RequestId = ActiveAttackRequestId;
	const float BlendOutTime = ResolveAttackTransitionBlendOutTime();
	CancelAttack(RequestId, BlendOutTime);
	return true;
}

void URoverCombatComponent::SetLightAttackHeld(const bool bHeld)
{
	bLightAttackHeld = bHeld;
}

const FRoverAttackDefinition* URoverCombatComponent::GetAttackDefinition(const int32 ComboIndex) const
{
	if (ComboIndex <= 0)
	{
		return nullptr;
	}

	const TArray<FRoverAttackDefinition>& ConfiguredChain = GetSettings().LightAttackChain;
	if (ConfiguredChain.IsValidIndex(ComboIndex - 1))
	{
		return &ConfiguredChain[ComboIndex - 1];
	}
	return FallbackSettings.LightAttackChain.IsValidIndex(ComboIndex - 1)
		? &FallbackSettings.LightAttackChain[ComboIndex - 1]
		: nullptr;
}

int32 URoverCombatComponent::GetLightAttackCount() const
{
	const int32 ConfiguredCount = GetSettings().LightAttackChain.Num();
	return ConfiguredCount > 0 ? ConfiguredCount : FallbackSettings.LightAttackChain.Num();
}

bool URoverCombatComponent::StartAttackSegment(const int32 ComboIndex, const int32 PreviousRequestId)
{
	const FRoverAttackDefinition* Definition = GetAttackDefinition(ComboIndex);
	if (!CharacterOwner || !Definition || Definition->Montage.IsNull())
	{
		return false;
	}

	AttackRequestSerial = AttackRequestSerial == MAX_int32 ? 1 : AttackRequestSerial + 1;
	const int32 RequestId = AttackRequestSerial;
	bool bMovementAccepted = PreviousRequestId > 0 &&
		CharacterOwner->TransferCombatMovementRestriction(PreviousRequestId, RequestId);
	if (!bMovementAccepted)
	{
		bMovementAccepted = CharacterOwner->TryBeginCombatMovementRestriction(RequestId);
	}
	if (!bMovementAccepted)
	{
		return false;
	}

	if (PreviousRequestId > 0)
	{
		CharacterOwner->CancelCombatAttackAdvance(PreviousRequestId);
	}
	DisableWeaponTrace();
	ActiveAttackRequestId = RequestId;
	CurrentComboIndex = ComboIndex;
	CombatPhase = ERoverCombatPhase::Startup;
	AttackWatchdogElapsed = 0.0f;
	AttackInputBufferRemaining = 0.0f;
	ComboResetRemaining = 0.0f;
	bAttackAnimationAcknowledged = false;
	bBufferedAttackInput = false;
	bComboWindowOpen = false;
	HitActorsThisAttack.Reset();
	CharacterOwner->SetCombatWeaponHand(Definition->WeaponHand);
	if (PreviousRequestId > 0)
	{
		CharacterOwner->PlayCombatAttackImmediately(RequestId);
	}
	return true;
}

bool URoverCombatComponent::TransitionToNextAttack()
{
	const int32 AttackCount = GetLightAttackCount();
	if (CurrentComboIndex <= 0 || AttackCount <= 0)
	{
		return false;
	}
	const int32 NextComboIndex = CurrentComboIndex >= AttackCount ? 1 : CurrentComboIndex + 1;
	return StartAttackSegment(NextComboIndex, ActiveAttackRequestId);
}

void URoverCombatComponent::AcknowledgeAttackStarted(const int32 RequestId)
{
	if (RequestId != ActiveAttackRequestId || CombatPhase != ERoverCombatPhase::Startup)
	{
		return;
	}

	bAttackAnimationAcknowledged = true;
	AttackWatchdogElapsed = 0.0f;
	if (CharacterOwner)
	{
		CharacterOwner->SetCombatWeaponVisible(true);
		if (const FRoverAttackDefinition* Definition = GetAttackDefinition(CurrentComboIndex))
		{
			CharacterOwner->StartCombatAttackAdvance(
				RequestId,
				Definition->AdvanceDistance,
				Definition->AdvanceDuration);
		}
	}
}

void URoverCombatComponent::BeginAttackActive(const int32 RequestId)
{
	if (RequestId != ActiveAttackRequestId || CombatPhase != ERoverCombatPhase::Startup)
	{
		return;
	}

	CombatPhase = ERoverCombatPhase::Active;
	EnableWeaponTrace();
}

void URoverCombatComponent::EndAttackActive(const int32 RequestId)
{
	if (RequestId != ActiveAttackRequestId || CombatPhase != ERoverCombatPhase::Active)
	{
		return;
	}
	// The end notify can fire between component ticks, so capture its evaluated
	// weapon pose before clearing the trace history.
	PerformWeaponTrace();
	DisableWeaponTrace();
}

void URoverCombatComponent::BeginComboWindow(const int32 RequestId)
{
	if (RequestId == ActiveAttackRequestId && CombatPhase != ERoverCombatPhase::None)
	{
		bComboWindowOpen = true;
		const bool bHasValidBufferedInput = bBufferedAttackInput && AttackInputBufferRemaining > 0.0f;
		if (bHasValidBufferedInput)
		{
			bBufferedAttackInput = false;
			AttackInputBufferRemaining = 0.0f;
			TransitionToNextAttack();
		}
	}
}

void URoverCombatComponent::EndComboWindow(const int32 RequestId)
{
	if (RequestId == ActiveAttackRequestId)
	{
		bComboWindowOpen = false;
	}
}

void URoverCombatComponent::BeginAttackRecovery(const int32 RequestId)
{
	if (RequestId != ActiveAttackRequestId || CombatPhase == ERoverCombatPhase::None)
	{
		return;
	}

	DisableWeaponTrace();
	CombatPhase = ERoverCombatPhase::Recovery;
	if (CharacterOwner)
	{
		CharacterOwner->CancelCombatAttackAdvance(RequestId);
		CharacterOwner->EndCombatMovementRestriction(RequestId, false);
	}
}

void URoverCombatComponent::FinishAttack(const int32 RequestId)
{
	if (RequestId != ActiveAttackRequestId || CombatPhase == ERoverCombatPhase::None)
	{
		return;
	}

	DisableWeaponTrace();
	if (CharacterOwner)
	{
		CharacterOwner->CancelCombatAttackAdvance(RequestId);
		CharacterOwner->EndCombatMovementRestriction(RequestId, false);
	}
}

void URoverCombatComponent::HandleAttackMontageEnded(const int32 RequestId, const bool bInterrupted)
{
	if (RequestId != ActiveAttackRequestId)
	{
		return;
	}
	if (bInterrupted)
	{
		CancelAttack(RequestId);
		return;
	}
	CompleteAttackSegment(RequestId);
}

void URoverCombatComponent::RejectAttackAnimation(const int32 RequestId)
{
	CancelAttack(RequestId);
}

void URoverCombatComponent::CancelAttack(const int32 RequestId, const float MontageBlendOutTime)
{
	if (RequestId == 0 || RequestId != ActiveAttackRequestId)
	{
		return;
	}

	DisableWeaponTrace();
	if (CharacterOwner)
	{
		CharacterOwner->CancelCombatAttackAdvance(RequestId);
		CharacterOwner->EndCombatMovementRestriction(RequestId);
	}
	ResetAttackState(true);
	if (CharacterOwner)
	{
		CharacterOwner->SetCombatWeaponVisible(false);
		CharacterOwner->StopCombatAttack(RequestId, MontageBlendOutTime);
	}
}

void URoverCombatComponent::CompleteAttackSegment(const int32 RequestId)
{
	if (RequestId == 0 || RequestId != ActiveAttackRequestId)
	{
		return;
	}

	DisableWeaponTrace();
	if (CharacterOwner)
	{
		CharacterOwner->CancelCombatAttackAdvance(RequestId);
		CharacterOwner->EndCombatMovementRestriction(RequestId);
		CharacterOwner->SetCombatWeaponVisible(false);
	}
	ResetAttackState(false);
	ComboResetRemaining = FMath::Max(0.0f, GetSettings().ComboResetDuration);
	if (ComboResetRemaining <= 0.0f)
	{
		ResetComboState();
	}
}

void URoverCombatComponent::ResetAttackState(const bool bResetCombo)
{
	CombatPhase = ERoverCombatPhase::None;
	ActiveAttackRequestId = 0;
	bAttackAnimationAcknowledged = false;
	bBufferedAttackInput = false;
	bComboWindowOpen = false;
	AttackWatchdogElapsed = 0.0f;
	AttackInputBufferRemaining = 0.0f;
	HitActorsThisAttack.Reset();
	if (bResetCombo)
	{
		ResetComboState();
	}
}

void URoverCombatComponent::ResetComboState()
{
	CurrentComboIndex = -1;
	ComboResetRemaining = 0.0f;
}

void URoverCombatComponent::HandleReceivedHit(const FRoverCombatHit& Hit)
{
	if (!CharacterOwner || bDead)
	{
		return;
	}

	CancelAttack(ActiveAttackRequestId);
	CancelHitReaction(HitReactionRequestId);
	const FVector SourceDirection = (Hit.SourceLocation - CharacterOwner->GetActorLocation()).GetSafeNormal2D();
	const float SideAmount = FVector::DotProduct(CharacterOwner->GetActorRightVector(), SourceDirection);
	HitReactionType = SideAmount < 0.0f
		? ERoverHitReactionType::LightLeft
		: ERoverHitReactionType::LightRight;
	HitReactionRequestId = ++HitReactionRequestSerial;
	bInHitReaction = true;
	bHitReactionAcknowledged = false;
	HitReactionWatchdogElapsed = 0.0f;

	// Debug: visualize the hit reaction trigger.
	if (CVarDrawHitReaction.GetValueOnGameThread())
	{
		const FVector MyLoc = CharacterOwner->GetActorLocation();
		// Impact sphere - larger and bright orange.
		DrawDebugSphere(GetWorld(), Hit.ImpactPoint, 25.0f, 8, FColor::Orange, false, 0.5f, 0, 4.0f);
		// Direction arrow: source -> target.
		DrawDebugDirectionalArrow(GetWorld(), Hit.SourceLocation, Hit.ImpactPoint, 30.0f, FColor::Yellow, false, 0.5f, 0, 3.0f);
		// Label: hit reaction type
		const FString Label = (HitReactionType == ERoverHitReactionType::LightLeft)
			? TEXT("HIT REACTION <- Left")
			: TEXT("HIT REACTION Right ->");
		DrawDebugString(GetWorld(), MyLoc + FVector(0, 0, 120),
			FString::Printf(TEXT("%s  [%.0f dmg / %.0f poise]"), *Label, Hit.Damage, Hit.PoiseDamage),
			nullptr, FColor::Orange, 0.5f);
	}
}

void URoverCombatComponent::AcknowledgeHitReactionStarted(const int32 RequestId)
{
	if (bInHitReaction && RequestId == HitReactionRequestId)
	{
		bHitReactionAcknowledged = true;
		HitReactionWatchdogElapsed = 0.0f;
	}
}

void URoverCombatComponent::FinishHitReaction(const int32 RequestId)
{
	CancelHitReaction(RequestId);
}

void URoverCombatComponent::HandleHitReactionMontageEnded(const int32 RequestId, const bool bInterrupted)
{
	if (RequestId == HitReactionRequestId && (bInterrupted || bInHitReaction))
	{
		CancelHitReaction(RequestId);
	}
}

void URoverCombatComponent::RejectHitReactionAnimation(const int32 RequestId)
{
	CancelHitReaction(RequestId);
}

void URoverCombatComponent::CancelHitReaction(const int32 RequestId)
{
	if (RequestId == 0 || RequestId != HitReactionRequestId)
	{
		return;
	}
	bInHitReaction = false;
	bHitReactionAcknowledged = false;
	HitReactionType = ERoverHitReactionType::None;
	HitReactionRequestId = 0;
	HitReactionWatchdogElapsed = 0.0f;
}

void URoverCombatComponent::HandleDeath()
{
	bDead = true;
	CancelAttack(ActiveAttackRequestId);
	CancelHitReaction(HitReactionRequestId);
	if (CharacterOwner)
	{
		CharacterOwner->SetCombatWeaponVisible(false);
	}
}

void URoverCombatComponent::EnableWeaponTrace()
{
	if (!CharacterOwner || !CharacterOwner->IsCombatWeaponVisible() ||
		!CharacterOwner->GetWeaponTraceLocations(PreviousTraceBase, PreviousTraceTip))
	{
		bWeaponTraceActive = false;
		return;
	}
	bWeaponTraceActive = true;
}

void URoverCombatComponent::DisableWeaponTrace()
{
	bWeaponTraceActive = false;
	PreviousTraceBase = FVector::ZeroVector;
	PreviousTraceTip = FVector::ZeroVector;
}

void URoverCombatComponent::PerformWeaponTrace()
{
	if (!CharacterOwner || !CharacterOwner->IsCombatWeaponVisible())
	{
		DisableWeaponTrace();
		return;
	}

	FVector CurrentBase;
	FVector CurrentTip;
	if (!CharacterOwner->GetWeaponTraceLocations(CurrentBase, CurrentTip))
	{
		DisableWeaponTrace();
		return;
	}

	const FRoverAttackDefinition* Definition = GetAttackDefinition(CurrentComboIndex);
	if (!Definition)
	{
		DisableWeaponTrace();
		return;
	}

	const FVector BaseTravel = CurrentBase - PreviousTraceBase;
	const FVector TipTravel = CurrentTip - PreviousTraceTip;
	const float MaxEndpointTravel = FMath::Max(BaseTravel.Size(), TipTravel.Size());
	const float SubstepDistance = FMath::Max(1.0f, Definition->TraceSubstepDistance);
	const int32 SubstepCount = FMath::Clamp(
		FMath::CeilToInt(MaxEndpointTravel / SubstepDistance),
		1,
		FMath::Clamp(Definition->MaxTraceSubsteps, 1, 16));
	const int32 BladeSampleCount = FMath::Clamp(Definition->TraceSampleCount, 2, 16);

	FVector FrameImpactDirection = ((CurrentBase + CurrentTip) -
		(PreviousTraceBase + PreviousTraceTip)).GetSafeNormal();
	if (FrameImpactDirection.IsNearlyZero())
	{
		FrameImpactDirection = BaseTravel.SizeSquared() > TipTravel.SizeSquared()
			? BaseTravel.GetSafeNormal()
			: TipTravel.GetSafeNormal();
	}
	if (FrameImpactDirection.IsNearlyZero())
	{
		FrameImpactDirection = CharacterOwner->GetActorForwardVector();
	}

	for (int32 SubstepIndex = 0; SubstepIndex < SubstepCount; ++SubstepIndex)
	{
		const float PreviousAlpha = static_cast<float>(SubstepIndex) / SubstepCount;
		const float CurrentAlpha = static_cast<float>(SubstepIndex + 1) / SubstepCount;
		const FVector StepPreviousBase = FMath::Lerp(PreviousTraceBase, CurrentBase, PreviousAlpha);
		const FVector StepPreviousTip = FMath::Lerp(PreviousTraceTip, CurrentTip, PreviousAlpha);
		const FVector StepCurrentBase = FMath::Lerp(PreviousTraceBase, CurrentBase, CurrentAlpha);
		const FVector StepCurrentTip = FMath::Lerp(PreviousTraceTip, CurrentTip, CurrentAlpha);

		// Point trajectories plus each interpolated blade pose form a query grid
		// over the frame's swept area instead of tracing only base and tip.
		for (int32 SampleIndex = 0; SampleIndex < BladeSampleCount; ++SampleIndex)
		{
			const float BladeAlpha = static_cast<float>(SampleIndex) / (BladeSampleCount - 1);
			const FVector SampleStart = FMath::Lerp(StepPreviousBase, StepPreviousTip, BladeAlpha);
			const FVector SampleEnd = FMath::Lerp(StepCurrentBase, StepCurrentTip, BladeAlpha);
			const FVector SampleDirection = (SampleEnd - SampleStart).GetSafeNormal();
			ProcessTraceSegment(
				SampleStart,
				SampleEnd,
				SampleDirection.IsNearlyZero() ? FrameImpactDirection : SampleDirection);
		}
		ProcessTraceSegment(StepCurrentBase, StepCurrentTip, FrameImpactDirection);
	}

	if (CVarDrawAttackTrace.GetValueOnGameThread())
	{
		const float Duration = FMath::Max(0.01f, CVarDrawAttackTraceDuration.GetValueOnGameThread());
		const float Radius = FMath::Max(0.0f, Definition->TraceRadius);
		DrawDebugLine(GetWorld(), CurrentBase, CurrentTip, FColor::White, false, Duration, 0, 1.5f);
		DrawDebugSphere(GetWorld(), CurrentBase, Radius, 12, FColor::Green, false, Duration, 0, 2.0f);
		DrawDebugSphere(GetWorld(), CurrentTip, Radius, 12, FColor::Magenta, false, Duration, 0, 2.0f);
	}

	PreviousTraceBase = CurrentBase;
	PreviousTraceTip = CurrentTip;
}

void URoverCombatComponent::ProcessTraceSegment(
	const FVector& Start,
	const FVector& End,
	const FVector& ImpactDirection)
{
	if (!CharacterOwner || !GetWorld())
	{
		return;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RoverWeaponTrace), false, CharacterOwner);
	TArray<FHitResult> Hits;
	const FRoverAttackDefinition* Definition = GetAttackDefinition(CurrentComboIndex);
	if (!Definition)
	{
		return;
	}
	const bool bHit = GetWorld()->SweepMultiByChannel(
		Hits,
		Start,
		End,
		FQuat::Identity,
		ECC_GameTraceChannel1,
		FCollisionShape::MakeSphere(FMath::Max(0.0f, Definition->TraceRadius)),
		QueryParams);

	if (CVarDrawAttackTrace.GetValueOnGameThread())
	{
		const float Duration = FMath::Max(0.01f, CVarDrawAttackTraceDuration.GetValueOnGameThread());
		const float Radius = FMath::Max(0.0f, Definition->TraceRadius);
		const FVector SweepDelta = End - Start;
		const float SweepLength = SweepDelta.Size();
		const FVector CapsuleCenter = (Start + End) * 0.5f;
		const FQuat CapsuleRotation = SweepLength > KINDA_SMALL_NUMBER
			? FQuat::FindBetweenNormals(FVector::UpVector, SweepDelta / SweepLength)
			: FQuat::Identity;
		DrawDebugCapsule(
			GetWorld(),
			CapsuleCenter,
			Radius + SweepLength * 0.5f,
			Radius,
			CapsuleRotation,
			bHit ? FColor::Red : FColor::Cyan,
			false,
			Duration,
			0,
			1.25f);
	}
	if (!bHit)
	{
		return;
	}

	for (const FHitResult& Hit : Hits)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || HitActor == CharacterOwner || HitActorsThisAttack.Contains(HitActor))
		{
			continue;
		}

		HitActorsThisAttack.Add(HitActor);

		// Debug: flash the hit impact point.
		if (CVarDrawAttackTrace.GetValueOnGameThread())
		{
			DrawDebugSphere(GetWorld(), Hit.ImpactPoint, 15.0f, 6, FColor::Red, false, 0.3f, 0, 3.0f);
			DrawDebugString(GetWorld(), Hit.ImpactPoint + FVector(0, 0, 30),
				FString::Printf(TEXT("HIT %.0f dmg"), Definition->Damage),
				nullptr, FColor::Yellow, 0.5f);
		}

		if (ARoverCharacter* HitCharacter = Cast<ARoverCharacter>(HitActor))
		{
			FRoverCombatHit CombatHit;
			CombatHit.Damage = Definition->Damage;
			CombatHit.PoiseDamage = Definition->PoiseDamage;
			CombatHit.ImpactPoint = Hit.ImpactPoint;
			CombatHit.SourceLocation = CharacterOwner->GetActorLocation();
			CombatHit.AttackRequestId = ActiveAttackRequestId;
			HitCharacter->ReceiveCombatHit(CombatHit);
			continue;
		}

		if (UWorldInteractionSubsystem* InteractionSubsystem =
			GetWorld()->GetSubsystem<UWorldInteractionSubsystem>())
		{
			FWorldInteractionRequest Request;
			Request.RequestId = FGuid::NewGuid();
			Request.Kind = EWorldInteractionKind::DirectHit;
			Request.Element = EWorldElementType::Physical;
			Request.SourceActor = CharacterOwner;
			Request.InstigatorController = CharacterOwner->GetController();
			Request.Hit = Hit;
			Request.Origin = Hit.ImpactPoint;
			Request.Direction = ImpactDirection.GetSafeNormal();
			Request.Damage = Definition->Damage;
			Request.ImpulseStrength = Definition->EnvironmentImpulseStrength;
			InteractionSubsystem->SubmitWorldInteraction(Request);
		}
	}
}
