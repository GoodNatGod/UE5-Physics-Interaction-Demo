#include "RoverCombatComponent.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RoverCharacter.h"
#include "WorldInteractionSubsystem.h"

namespace
{
float SmoothStepAlpha(const float Alpha)
{
	const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	return ClampedAlpha * ClampedAlpha * (3.0f - 2.0f * ClampedAlpha);
}

FTransform BlendThrowTransform(const FTransform& Start, const FTransform& End, const float Alpha)
{
	const float EasedAlpha = SmoothStepAlpha(Alpha);
	return FTransform(
		FQuat::Slerp(Start.GetRotation(), End.GetRotation(), EasedAlpha).GetNormalized(),
		FMath::Lerp(Start.GetLocation(), End.GetLocation(), EasedAlpha),
		FMath::Lerp(Start.GetScale3D(), End.GetScale3D(), EasedAlpha));
}
}

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
	ResetAttackInputDecision();
	EndThirdAttackWeaponThrow(true);
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

	if (bPendingAttackDecision && bLightAttackHeld)
	{
		LightAttackHoldElapsed += DeltaTime;
		if (LightAttackHoldElapsed >= FMath::Max(0.05f, GetSettings().HeavyAttackHoldThreshold) &&
			RequestHeavyAttack())
		{
			bPendingAttackDecision = false;
			bAttackInputRoutedImmediately = false;
			bAttackInputStartedFromThirdLightAttack = false;
		}
	}

	if (bBufferedAttackInput)
	{
		AttackInputBufferRemaining = FMath::Max(0.0f, AttackInputBufferRemaining - DeltaTime);
		if (AttackInputBufferRemaining <= 0.0f)
		{
			bBufferedAttackInput = false;
			BufferedAttackDirection = FVector::ZeroVector;
		}
	}

	if (CombatPhase != ERoverCombatPhase::None)
	{
		AttackWatchdogElapsed += DeltaTime;
		const FRoverCombatSettings& Settings = GetSettings();
		float Timeout = Settings.AttackPendingTimeout;
		if (bAttackAnimationAcknowledged)
		{
			const UAnimMontage* Montage = ResolveAttackMontage();
			const float ExpectedPlaybackDuration = Montage
				? Montage->GetPlayLength() / ResolveAttackPlayRate()
				: 0.0f;
			if (CurrentAttackType == ERoverAttackType::AirAttack && !bAirAttackLanded)
			{
				Timeout = FMath::Max(0.1f, Settings.AirAttackMaximumDuration);
			}
			else
			{
				Timeout = FMath::Max(
					Settings.AttackActiveTimeout,
					ExpectedPlaybackDuration + Settings.AttackPendingTimeout);
			}
		}
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

	if (CombatPhase == ERoverCombatPhase::None &&
		bResonanceTriggerWindow &&
		ResonanceTriggerRemaining > 0.0f)
	{
		ResonanceTriggerRemaining = FMath::Max(0.0f, ResonanceTriggerRemaining - DeltaTime);
		if (ResonanceTriggerRemaining <= 0.0f)
		{
			bResonanceTriggerWindow = false;
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

	UpdateThirdAttackWeaponThrow(DeltaTime);

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
	const FRoverAttackDefinition* Definition = GetActiveAttackDefinition();
	return Definition ? Definition->Montage.LoadSynchronous() : nullptr;
}

float URoverCombatComponent::ResolveAttackPlayRate() const
{
	const FRoverAttackDefinition* Definition = GetActiveAttackDefinition();
	return Definition ? FMath::Max(0.1f, Definition->AnimPlayRate) : 1.0f;
}

float URoverCombatComponent::ResolveAirAttackAscentDuration() const
{
	const UAnimMontage* Montage = ResolveAttackMontage();
	const float PlayRate = ResolveAttackPlayRate();
	if (!Montage)
	{
		return 0.0f;
	}

	float StartedTime = 0.0f;
	float ApexTime = INDEX_NONE;
	for (const FAnimNotifyEvent& Notify : Montage->Notifies)
	{
		if (Notify.NotifyName == TEXT("RoverAttackStarted"))
		{
			StartedTime = Notify.GetTriggerTime();
		}
		else if (Notify.NotifyName == TEXT("RoverAirAttackApex"))
		{
			ApexTime = Notify.GetTriggerTime();
		}
	}
	if (ApexTime > StartedTime)
	{
		return (ApexTime - StartedTime) / PlayRate;
	}

	if (!Montage->SlotAnimTracks.IsEmpty() &&
		!Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsEmpty())
	{
		const FAnimSegment& StartSegment = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
		if (const UAnimSequence* StartSequence = Cast<UAnimSequence>(StartSegment.GetAnimReference()))
		{
			const FFrameRate SamplingRate = StartSequence->GetSamplingFrameRate();
			if (SamplingRate.IsValid())
			{
				return static_cast<float>(SamplingRate.AsSeconds(
					FFrameTime(FMath::Max(1, GetSettings().AirAttackApexFrame)))) / PlayRate;
			}
		}
	}

	return 0.0f;
}

float URoverCombatComponent::ResolveAttackTransitionBlendOutTime() const
{
	const FRoverAttackDefinition* Definition = GetActiveAttackDefinition();
	return Definition ? FMath::Max(0.0f, Definition->MontageBlendOutTime) : 0.0f;
}

UAnimMontage* URoverCombatComponent::ResolveHitReactionMontage() const
{
	const FRoverCombatSettings& Settings = GetSettings();
	return HitReactionType == ERoverHitReactionType::LightLeft
		? Settings.LightHitLeftMontage.LoadSynchronous()
		: Settings.LightHitRightMontage.LoadSynchronous();
}

bool URoverCombatComponent::RequestAttack()
{
	if (!CharacterOwner || bDead || bInHitReaction)
	{
		return false;
	}
	if (CombatPhase == ERoverCombatPhase::None &&
		CharacterOwner->GetCharacterMovement() && CharacterOwner->GetCharacterMovement()->IsFalling())
	{
		return RequestAirAttack();
	}

	if (CanStartHeavyResonance())
	{
		return StartHeavyResonance(ResolveAttackInputDirection());
	}

	if (CombatPhase == ERoverCombatPhase::None)
	{
		const int32 AttackCount = GetLightAttackCount();
		const bool bContinueCombo = CurrentComboIndex >= 1 && ComboResetRemaining > 0.0f;
		const int32 NextComboIndex = bContinueCombo && AttackCount > 0
			? (CurrentComboIndex >= AttackCount ? 1 : CurrentComboIndex + 1)
			: 1;
		return StartAttackSegment(NextComboIndex, ResolveAttackInputDirection());
	}
	if (CurrentAttackType == ERoverAttackType::HeavyResonance)
	{
		if (bComboWindowOpen || CombatPhase == ERoverCombatPhase::Recovery)
		{
			bBufferedAttackInput = false;
			AttackInputBufferRemaining = 0.0f;
			BufferedAttackDirection = FVector::ZeroVector;
			return StartFirstLightAttackAfterResonance(ResolveAttackInputDirection());
		}

		bBufferedAttackInput = true;
		BufferedAttackDirection = ResolveAttackInputDirection();
		AttackInputBufferRemaining = FMath::Max(0.0f, GetSettings().AttackInputBufferDuration);
		return true;
	}

	if (CurrentComboIndex <= 0 || GetLightAttackCount() <= 0)
	{
		return false;
	}

	if (bComboWindowOpen)
	{
		bBufferedAttackInput = false;
		AttackInputBufferRemaining = 0.0f;
		BufferedAttackDirection = FVector::ZeroVector;
		return TransitionToNextAttack(ResolveAttackInputDirection());
	}

	bBufferedAttackInput = true;
	BufferedAttackDirection = ResolveAttackInputDirection();
	AttackInputBufferRemaining = FMath::Max(0.0f, GetSettings().AttackInputBufferDuration);
	return true;
}

bool URoverCombatComponent::RequestAirAttack()
{
	if (!CharacterOwner || bDead || bInHitReaction || CombatPhase != ERoverCombatPhase::None ||
		!CharacterOwner->GetCharacterMovement() || !CharacterOwner->GetCharacterMovement()->IsFalling())
	{
		return false;
	}

	const FRoverCombatSettings& Settings = GetSettings();
	const FRoverAttackDefinition& Definition = Settings.AirAttackDefinition;
	if (Definition.Montage.IsNull())
	{
		return false;
	}

	AttackRequestSerial = AttackRequestSerial == MAX_int32 ? 1 : AttackRequestSerial + 1;
	const int32 RequestId = AttackRequestSerial;
	if (!CharacterOwner->TryBeginAirCombatMovementRestriction(
		RequestId,
		Settings.AirAttackHorizontalVelocityScale))
	{
		return false;
	}

	ResetAttackInputDecision();
	EndThirdAttackWeaponThrow(true);
	DisableWeaponTrace();
	ResetComboState();
	ActiveAttackDirection = ResolveAttackInputDirection().GetSafeNormal2D();
	if (ActiveAttackDirection.IsNearlyZero())
	{
		ActiveAttackDirection = CharacterOwner->GetActorForwardVector().GetSafeNormal2D();
	}
	CharacterOwner->FaceCombatDirection(ActiveAttackDirection);
	ActiveAttackRequestId = RequestId;
	CurrentComboIndex = -1;
	CurrentAttackType = ERoverAttackType::AirAttack;
	CombatPhase = ERoverCombatPhase::Startup;
	AttackWatchdogElapsed = 0.0f;
	AttackInputBufferRemaining = 0.0f;
	bAttackAnimationAcknowledged = false;
	bBufferedAttackInput = false;
	bComboWindowOpen = false;
	bResonanceWindowOpen = false;
	bResonanceTriggerWindow = false;
	bAirAttackLanded = false;
	ResonanceTriggerRemaining = 0.0f;
	HitActorsThisAttack.Reset();
	CharacterOwner->SetCombatWeaponHand(Definition.WeaponHand);
	return true;
}

bool URoverCombatComponent::RequestHeavyAttack()
{
	if (!CharacterOwner || bDead || bInHitReaction)
	{
		return false;
	}
	if (bAttackInputStartedFromThirdLightAttack && CanStartHeavyResonance())
	{
		return StartHeavyResonance(ResolveAttackInputDirection());
	}
	const bool bInterruptingLightAttack =
		CombatPhase != ERoverCombatPhase::None &&
		CurrentAttackType == ERoverAttackType::LightAttack &&
		ActiveAttackRequestId > 0;
	if (CombatPhase != ERoverCombatPhase::None && !bInterruptingLightAttack)
	{
		return false;
	}

	const FRoverAttackDefinition& Definition = GetSettings().HeavyAttackDefinition;
	if (Definition.Montage.IsNull())
	{
		return false;
	}

	const int32 PreviousRequestId = bInterruptingLightAttack ? ActiveAttackRequestId : 0;
	const float PreviousBlendOutTime = bInterruptingLightAttack
		? ResolveAttackTransitionBlendOutTime()
		: 0.0f;
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
	EndThirdAttackWeaponThrow(true);
	DisableWeaponTrace();
	ActiveAttackDirection = ResolveAttackInputDirection().GetSafeNormal2D();
	if (ActiveAttackDirection.IsNearlyZero())
	{
		ActiveAttackDirection = CharacterOwner->GetActorForwardVector().GetSafeNormal2D();
	}
	CharacterOwner->FaceCombatDirection(ActiveAttackDirection);
	ActiveAttackRequestId = RequestId;
	CurrentComboIndex = 0;
	if (bInterruptingLightAttack)
	{
		PreviousAttackType = ERoverAttackType::LightAttack;
	}
	CurrentAttackType = ERoverAttackType::HeavyAttack;
	CombatPhase = ERoverCombatPhase::Startup;
	AttackWatchdogElapsed = 0.0f;
	AttackInputBufferRemaining = 0.0f;
	ComboResetRemaining = 0.0f;
	bAttackAnimationAcknowledged = false;
	bBufferedAttackInput = false;
	bComboWindowOpen = false;
	bResonanceWindowOpen = false;
	bResonanceTriggerWindow = false;
	ResonanceTriggerRemaining = 0.0f;
	BufferedAttackDirection = FVector::ZeroVector;
	HitActorsThisAttack.Reset();
	CharacterOwner->SetCombatWeaponHand(Definition.WeaponHand);
	if (PreviousRequestId > 0)
	{
		CharacterOwner->StopCombatAttack(PreviousRequestId, PreviousBlendOutTime);
		CharacterOwner->PlayCombatAttackImmediately(RequestId);
	}
	return true;
}

bool URoverCombatComponent::StartHeavyResonance(const FVector& AttackDirection)
{
	if (!CharacterOwner || bDead || bInHitReaction || !CanStartHeavyResonance())
	{
		return false;
	}

	const FRoverAttackDefinition& Definition = GetSettings().HeavyResonanceDefinition;
	const int32 PreviousRequestId = ActiveAttackRequestId;
	const float PreviousBlendOutTime = PreviousRequestId > 0
		? ResolveAttackTransitionBlendOutTime()
		: 0.0f;
	const ERoverAttackType SourceAttackType = CurrentAttackType != ERoverAttackType::None
		? CurrentAttackType
		: PreviousAttackType;
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
	EndThirdAttackWeaponThrow(true);
	DisableWeaponTrace();
	ActiveAttackDirection = AttackDirection.GetSafeNormal2D();
	if (ActiveAttackDirection.IsNearlyZero())
	{
		ActiveAttackDirection = CharacterOwner->GetActorForwardVector().GetSafeNormal2D();
	}
	CharacterOwner->FaceCombatDirection(ActiveAttackDirection);
	PreviousAttackType = SourceAttackType;
	ActiveAttackRequestId = RequestId;
	CurrentComboIndex = 0;
	CurrentAttackType = ERoverAttackType::HeavyResonance;
	CombatPhase = ERoverCombatPhase::Startup;
	AttackWatchdogElapsed = 0.0f;
	AttackInputBufferRemaining = 0.0f;
	ComboResetRemaining = 0.0f;
	ResonanceTriggerRemaining = 0.0f;
	bAttackAnimationAcknowledged = false;
	bBufferedAttackInput = false;
	bComboWindowOpen = false;
	bResonanceWindowOpen = false;
	bResonanceTriggerWindow = false;
	BufferedAttackDirection = FVector::ZeroVector;
	HitActorsThisAttack.Reset();
	CharacterOwner->SetCombatWeaponHand(Definition.WeaponHand);
	if (PreviousRequestId > 0)
	{
		CharacterOwner->StopCombatAttack(PreviousRequestId, PreviousBlendOutTime);
		CharacterOwner->PlayCombatAttackImmediately(RequestId);
	}
	return true;
}

bool URoverCombatComponent::BeginAttackInput()
{
	if (!CharacterOwner || bDead || bInHitReaction)
	{
		ResetAttackInputDecision();
		return false;
	}
	if (CombatPhase == ERoverCombatPhase::None &&
		CharacterOwner->GetCharacterMovement() && CharacterOwner->GetCharacterMovement()->IsFalling())
	{
		ResetAttackInputDecision();
		return RequestAirAttack();
	}

	if (CanStartHeavyResonance())
	{
		ResetAttackInputDecision();
		return RequestAttack();
	}

	bAttackInputStartedFromThirdLightAttack =
		CombatPhase != ERoverCombatPhase::None &&
		CurrentAttackType == ERoverAttackType::LightAttack &&
		CurrentComboIndex == 3;
	bLightAttackHeld = true;
	LightAttackHoldElapsed = 0.0f;
	bPendingAttackDecision = true;
	bAttackInputRoutedImmediately = false;
	const bool bCanContinueLightCombo =
		CombatPhase != ERoverCombatPhase::None ||
		(CurrentComboIndex >= 1 && ComboResetRemaining > 0.0f);
	if (bCanContinueLightCombo)
	{
		if (CombatPhase != ERoverCombatPhase::None &&
			CurrentAttackType != ERoverAttackType::LightAttack &&
			CurrentAttackType != ERoverAttackType::HeavyResonance)
		{
			ResetAttackInputDecision();
			return false;
		}
		bAttackInputRoutedImmediately = true;
		return RequestAttack();
	}

	return true;
}

bool URoverCombatComponent::EndAttackInput()
{
	bLightAttackHeld = false;
	if (!bPendingAttackDecision)
	{
		LightAttackHoldElapsed = 0.0f;
		bAttackInputRoutedImmediately = false;
		bAttackInputStartedFromThirdLightAttack = false;
		return false;
	}

	bPendingAttackDecision = false;
	LightAttackHoldElapsed = 0.0f;
	const bool bWasRoutedImmediately = bAttackInputRoutedImmediately;
	bAttackInputRoutedImmediately = false;
	bAttackInputStartedFromThirdLightAttack = false;
	return bWasRoutedImmediately ? true : RequestAttack();
}

bool URoverCombatComponent::RequestDodgeInterrupt()
{
	if (bDead || bInHitReaction)
	{
		return false;
	}

	ResetAttackInputDecision();
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
	if (!bHeld && !bPendingAttackDecision)
	{
		LightAttackHoldElapsed = 0.0f;
	}
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

const FRoverAttackDefinition* URoverCombatComponent::GetActiveAttackDefinition() const
{
	if (CurrentAttackType == ERoverAttackType::AirAttack)
	{
		return &GetSettings().AirAttackDefinition;
	}
	if (CurrentAttackType == ERoverAttackType::HeavyAttack)
	{
		return &GetSettings().HeavyAttackDefinition;
	}
	if (CurrentAttackType == ERoverAttackType::HeavyResonance)
	{
		return &GetSettings().HeavyResonanceDefinition;
	}
	if (CurrentAttackType == ERoverAttackType::LightAttack)
	{
		return GetAttackDefinition(CurrentComboIndex);
	}
	return nullptr;
}

bool URoverCombatComponent::CanStartHeavyResonance() const
{
	if (GetSettings().HeavyResonanceDefinition.Montage.IsNull())
	{
		return false;
	}
	if (CombatPhase != ERoverCombatPhase::None)
	{
		const bool bFromThirdAttackHold =
			bAttackInputStartedFromThirdLightAttack &&
			bPendingAttackDecision &&
			bLightAttackHeld &&
			LightAttackHoldElapsed >= FMath::Max(0.05f, GetSettings().HeavyAttackHoldThreshold) &&
			CurrentAttackType == ERoverAttackType::LightAttack &&
			ActiveAttackRequestId > 0;
		const bool bFromThirdLightAttack =
			CurrentAttackType == ERoverAttackType::LightAttack &&
			CurrentComboIndex == 3 &&
			(bResonanceWindowOpen ||
				(CombatPhase == ERoverCombatPhase::Recovery && bResonanceTriggerWindow));
		const bool bFromHeavyRecovery =
			CurrentAttackType == ERoverAttackType::HeavyAttack &&
			CombatPhase == ERoverCombatPhase::Recovery &&
			bResonanceTriggerWindow;
		return bFromThirdAttackHold || bFromThirdLightAttack || bFromHeavyRecovery;
	}

	return bResonanceTriggerWindow &&
		(PreviousAttackType == ERoverAttackType::HeavyAttack ||
			(PreviousAttackType == ERoverAttackType::LightAttack && CurrentComboIndex == 3));
}

int32 URoverCombatComponent::GetLightAttackCount() const
{
	const int32 ConfiguredCount = GetSettings().LightAttackChain.Num();
	return ConfiguredCount > 0 ? ConfiguredCount : FallbackSettings.LightAttackChain.Num();
}

FVector URoverCombatComponent::ResolveAttackInputDirection() const
{
	if (!CharacterOwner)
	{
		return FVector::ZeroVector;
	}

	if (GetSettings().bAllowDirectionalLightAttack)
	{
		const FVector InputDirection = CharacterOwner->GetCombatDirectionIntent();
		if (!InputDirection.IsNearlyZero())
		{
			return InputDirection;
		}
	}
	return CharacterOwner->GetActorForwardVector().GetSafeNormal2D();
}

bool URoverCombatComponent::StartAttackSegment(
	const int32 ComboIndex,
	const FVector& AttackDirection,
	const int32 PreviousRequestId)
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
	EndThirdAttackWeaponThrow(true);
	DisableWeaponTrace();
	ActiveAttackDirection = AttackDirection.GetSafeNormal2D();
	if (ActiveAttackDirection.IsNearlyZero())
	{
		ActiveAttackDirection = CharacterOwner->GetActorForwardVector().GetSafeNormal2D();
	}
	CharacterOwner->FaceCombatDirection(ActiveAttackDirection);
	ActiveAttackRequestId = RequestId;
	CurrentComboIndex = ComboIndex;
	CurrentAttackType = ERoverAttackType::LightAttack;
	CombatPhase = ERoverCombatPhase::Startup;
	AttackWatchdogElapsed = 0.0f;
	AttackInputBufferRemaining = 0.0f;
	ComboResetRemaining = 0.0f;
	bAttackAnimationAcknowledged = false;
	bBufferedAttackInput = false;
	bComboWindowOpen = false;
	bResonanceWindowOpen = false;
	bResonanceTriggerWindow = false;
	ResonanceTriggerRemaining = 0.0f;
	HitActorsThisAttack.Reset();
	CharacterOwner->SetCombatWeaponHand(Definition->WeaponHand);
	if (PreviousRequestId > 0)
	{
		CharacterOwner->PlayCombatAttackImmediately(RequestId);
	}
	return true;
}

bool URoverCombatComponent::TransitionToNextAttack(const FVector& AttackDirection)
{
	const int32 AttackCount = GetLightAttackCount();
	if (CurrentComboIndex <= 0 || AttackCount <= 0)
	{
		return false;
	}
	const int32 NextComboIndex = CurrentComboIndex >= AttackCount
		? 1
		: CurrentComboIndex + 1;
	return StartAttackSegment(NextComboIndex, AttackDirection, ActiveAttackRequestId);
}

bool URoverCombatComponent::StartFirstLightAttackAfterResonance(const FVector& AttackDirection)
{
	if (CurrentAttackType != ERoverAttackType::HeavyResonance || ActiveAttackRequestId <= 0)
	{
		return false;
	}

	const int32 PreviousRequestId = ActiveAttackRequestId;
	if (!StartAttackSegment(1, AttackDirection, PreviousRequestId))
	{
		return false;
	}
	PreviousAttackType = ERoverAttackType::HeavyResonance;
	return true;
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
		if (CurrentAttackType == ERoverAttackType::AirAttack)
		{
			if (!bAirAttackLanded)
			{
				CharacterOwner->BeginAirCombatAscent(
					RequestId,
					GetSettings().AirAttackAscentHeight,
					ResolveAirAttackAscentDuration());
			}
			BeginAttackActive(RequestId);
		}
		if (CurrentAttackType != ERoverAttackType::AirAttack)
		{
			if (const FRoverAttackDefinition* Definition = GetActiveAttackDefinition())
			{
				float MovementDistance = Definition->AdvanceDistance;
				float MovementDuration = Definition->AdvanceDuration;
				if (CurrentAttackType == ERoverAttackType::HeavyAttack)
				{
					MovementDistance = -FMath::Max(0.0f, GetSettings().HeavyAttackRetreatDistance);
					MovementDuration = GetSettings().HeavyAttackRetreatDuration;
				}
				else if (CurrentAttackType == ERoverAttackType::HeavyResonance)
				{
					MovementDistance = FMath::Max(0.0f, GetSettings().ResonanceDashDistance);
					MovementDuration = GetSettings().ResonanceDashDuration;
				}
				CharacterOwner->StartCombatAttackAdvance(
					RequestId,
					MovementDistance,
					MovementDuration);
			}
		}
		else if (bAirAttackLanded)
		{
			CharacterOwner->TransitionAirAttackToLanding(RequestId);
		}
		if (CurrentAttackType == ERoverAttackType::LightAttack && CurrentComboIndex == 3)
		{
			BeginThirdAttackWeaponThrow(RequestId);
		}
	}
}

void URoverCombatComponent::ReachAirAttackApex(const int32 RequestId)
{
	if (RequestId != ActiveAttackRequestId || CurrentAttackType != ERoverAttackType::AirAttack ||
		CombatPhase == ERoverCombatPhase::None || bAirAttackLanded || !CharacterOwner)
	{
		return;
	}

	CharacterOwner->BeginAirCombatDescent(RequestId, GetSettings().AirAttackDescentSpeed);
}

void URoverCombatComponent::BeginAttackActive(const int32 RequestId)
{
	if (RequestId != ActiveAttackRequestId || CombatPhase != ERoverCombatPhase::Startup)
	{
		return;
	}

	CombatPhase = ERoverCombatPhase::Active;
	if (IsThirdAttackWeaponThrowActive())
	{
		RefreshThirdAttackWeaponThrowTrace();
	}
	else
	{
		EnableWeaponTrace();
	}
}

void URoverCombatComponent::EndAttackActive(const int32 RequestId)
{
	if (RequestId != ActiveAttackRequestId || CombatPhase != ERoverCombatPhase::Active)
	{
		return;
	}
	// The end notify can fire between component ticks, so capture its evaluated
	// weapon pose before clearing the trace history.
	if (bWeaponTraceActive)
	{
		PerformWeaponTrace();
	}
	if (IsThirdAttackWeaponThrowActive())
	{
		RefreshThirdAttackWeaponThrowTrace();
	}
	else
	{
		DisableWeaponTrace();
	}
}

void URoverCombatComponent::BeginComboWindow(const int32 RequestId)
{
	if (RequestId == ActiveAttackRequestId && CombatPhase != ERoverCombatPhase::None)
	{
		bComboWindowOpen = true;
		const bool bHasValidBufferedInput = bBufferedAttackInput && AttackInputBufferRemaining > 0.0f;
		if (bHasValidBufferedInput)
		{
			const FVector BufferedDirection = BufferedAttackDirection;
			bBufferedAttackInput = false;
			AttackInputBufferRemaining = 0.0f;
			BufferedAttackDirection = FVector::ZeroVector;
			if (CurrentAttackType == ERoverAttackType::HeavyResonance)
			{
				StartFirstLightAttackAfterResonance(BufferedDirection);
			}
			else
			{
				TransitionToNextAttack(BufferedDirection);
			}
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

void URoverCombatComponent::BeginResonanceWindow(const int32 RequestId)
{
	if (RequestId == ActiveAttackRequestId &&
		CurrentAttackType == ERoverAttackType::LightAttack &&
		CurrentComboIndex == 3)
	{
		bResonanceWindowOpen = true;
	}
}

void URoverCombatComponent::EndResonanceWindow(const int32 RequestId)
{
	if (RequestId == ActiveAttackRequestId)
	{
		bResonanceWindowOpen = false;
	}
}

void URoverCombatComponent::BeginAttackRecovery(const int32 RequestId)
{
	if (RequestId != ActiveAttackRequestId || CombatPhase == ERoverCombatPhase::None)
	{
		return;
	}

	if (IsThirdAttackWeaponThrowActive())
	{
		RefreshThirdAttackWeaponThrowTrace();
	}
	else
	{
		DisableWeaponTrace();
	}
	CombatPhase = ERoverCombatPhase::Recovery;
	if ((CurrentAttackType == ERoverAttackType::LightAttack && CurrentComboIndex == 3) ||
		CurrentAttackType == ERoverAttackType::HeavyAttack)
	{
		bResonanceTriggerWindow = true;
		ResonanceTriggerRemaining = 0.0f;
	}
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

	if (IsThirdAttackWeaponThrowActive())
	{
		RefreshThirdAttackWeaponThrowTrace();
	}
	else
	{
		DisableWeaponTrace();
	}
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

bool URoverCombatComponent::HandleLanded(const float)
{
	if (CurrentAttackType != ERoverAttackType::AirAttack ||
		CombatPhase == ERoverCombatPhase::None || ActiveAttackRequestId <= 0)
	{
		return false;
	}

	bAirAttackLanded = true;
	AttackWatchdogElapsed = 0.0f;
	if (CharacterOwner)
	{
		CharacterOwner->TransitionAirAttackToLanding(ActiveAttackRequestId);
	}
	return true;
}

void URoverCombatComponent::CancelAttack(const int32 RequestId, const float MontageBlendOutTime)
{
	if (RequestId == 0 || RequestId != ActiveAttackRequestId)
	{
		return;
	}

	EndThirdAttackWeaponThrow(true);
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
	const ERoverAttackType CompletedAttackType = CurrentAttackType;
	const int32 CompletedComboIndex = CurrentComboIndex;
	const bool bOpenResonanceTrigger =
		CompletedAttackType == ERoverAttackType::HeavyAttack ||
		(CompletedAttackType == ERoverAttackType::LightAttack && CompletedComboIndex == 3);

	EndThirdAttackWeaponThrow(
		true,
		CurrentComboIndex == 3 ? ERoverWeaponHand::Left : ERoverWeaponHand::Right);
	DisableWeaponTrace();
	if (CharacterOwner)
	{
		CharacterOwner->CancelCombatAttackAdvance(RequestId);
		CharacterOwner->EndCombatMovementRestriction(RequestId);
		CharacterOwner->SetCombatWeaponVisible(false);
	}
	bResonanceWindowOpen = false;
	bAirAttackLanded = false;
	ResetAttackState(false);
	if (bOpenResonanceTrigger)
	{
		ResonanceTriggerRemaining = FMath::Max(0.0f, GetSettings().ResonanceTriggerWindowDuration);
		bResonanceTriggerWindow = ResonanceTriggerRemaining > 0.0f;
	}
	else
	{
		ResonanceTriggerRemaining = 0.0f;
		bResonanceTriggerWindow = false;
	}
	if (CompletedAttackType == ERoverAttackType::LightAttack)
	{
		ComboResetRemaining = FMath::Max(0.0f, GetSettings().ComboResetDuration);
		if (ComboResetRemaining <= 0.0f)
		{
			ResetComboState();
		}
	}
	else
	{
		CurrentComboIndex = -1;
		ComboResetRemaining = 0.0f;
	}
}

void URoverCombatComponent::ResetAttackState(const bool bResetCombo)
{
	EndThirdAttackWeaponThrow(true);
	if (!bResetCombo && CurrentAttackType != ERoverAttackType::None)
	{
		PreviousAttackType = CurrentAttackType;
	}
	CurrentAttackType = ERoverAttackType::None;
	CombatPhase = ERoverCombatPhase::None;
	ActiveAttackRequestId = 0;
	bAttackAnimationAcknowledged = false;
	bBufferedAttackInput = false;
	bComboWindowOpen = false;
	bResonanceWindowOpen = false;
	bAirAttackLanded = false;
	ActiveAttackDirection = FVector::ZeroVector;
	BufferedAttackDirection = FVector::ZeroVector;
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
	PreviousAttackType = ERoverAttackType::None;
	bResonanceWindowOpen = false;
	bResonanceTriggerWindow = false;
	ResonanceTriggerRemaining = 0.0f;
}

void URoverCombatComponent::ResetAttackInputDecision()
{
	bLightAttackHeld = false;
	bPendingAttackDecision = false;
	bAttackInputRoutedImmediately = false;
	bAttackInputStartedFromThirdLightAttack = false;
	LightAttackHoldElapsed = 0.0f;
}

void URoverCombatComponent::HandleReceivedHit(const FRoverCombatHit& Hit)
{
	if (!CharacterOwner || bDead)
	{
		return;
	}

	ResetAttackInputDecision();
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
	ResetAttackInputDecision();
	CancelAttack(ActiveAttackRequestId);
	CurrentAttackType = ERoverAttackType::None;
	PreviousAttackType = ERoverAttackType::None;
	CancelHitReaction(HitReactionRequestId);
	if (CharacterOwner)
	{
		CharacterOwner->SetCombatWeaponVisible(false);
	}
}

void URoverCombatComponent::BeginThirdAttackWeaponThrow(const int32 RequestId)
{
	EndThirdAttackWeaponThrow(true);
	const FRoverCombatSettings& Settings = GetSettings();
	if (!Settings.bEnableThirdAttackWeaponThrow || !CharacterOwner || CurrentComboIndex != 3 ||
		RequestId <= 0 || RequestId != ActiveAttackRequestId)
	{
		return;
	}

	ThirdAttackThrowRequestId = RequestId;
	ThirdAttackThrowPhaseElapsed = 0.0f;
	ThirdAttackThrowPhaseAlpha = 0.0f;
	ThirdAttackThrowSpinDegrees = 0.0f;
	if (Settings.ThirdAttackThrowStartDelay > KINDA_SMALL_NUMBER)
	{
		ThirdAttackThrowPhase = ERoverThirdAttackThrowPhase::Waiting;
		RefreshThirdAttackWeaponThrowTrace();
		return;
	}

	if (!BeginThirdAttackWeaponOutbound())
	{
		EndThirdAttackWeaponThrow(true);
	}
}

bool URoverCombatComponent::BeginThirdAttackWeaponOutbound()
{
	if (!CharacterOwner || ThirdAttackThrowRequestId <= 0 ||
		ThirdAttackThrowRequestId != ActiveAttackRequestId || CurrentComboIndex != 3)
	{
		return false;
	}

	USkeletalMeshComponent* Weapon = CharacterOwner->GetCombatWeapon();
	if (!Weapon || !Weapon->GetSkeletalMeshAsset())
	{
		return false;
	}

	const FRoverCombatSettings& Settings = GetSettings();
	ThirdAttackThrowStart = Weapon->GetComponentTransform();
	FVector Forward = ActiveAttackDirection.GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		Forward = CharacterOwner->GetActorForwardVector().GetSafeNormal2D();
	}
	const FVector Up = FVector::UpVector;
	FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
	if (Right.IsNearlyZero())
	{
		Right = CharacterOwner->GetActorRightVector().GetSafeNormal2D();
	}

	ThirdAttackThrowAnchor = ThirdAttackThrowStart;
	ThirdAttackThrowAnchor.SetLocation(
		CharacterOwner->GetActorLocation() +
		Forward * Settings.ThirdAttackThrowTargetForwardOffset +
		Right * Settings.ThirdAttackThrowTargetLateralOffset +
		Up * Settings.ThirdAttackThrowTargetHeightOffset);
	ThirdAttackThrowAnchor.SetRotation(
		(ThirdAttackThrowStart.GetRotation() *
			Settings.ThirdAttackThrowAnchorRotationOffset.Quaternion()).GetNormalized());

	const FVector ConfiguredAxis = Settings.ThirdAttackThrowSpinAxis;
	ThirdAttackThrowSpinAxisWorld =
		Forward * ConfiguredAxis.X + Right * ConfiguredAxis.Y + Up * ConfiguredAxis.Z;
	ThirdAttackThrowSpinAxisWorld = ThirdAttackThrowSpinAxisWorld.GetSafeNormal();
	if (ThirdAttackThrowSpinAxisWorld.IsNearlyZero())
	{
		ThirdAttackThrowSpinAxisWorld = Right;
	}

	if (!CharacterOwner->DetachCombatWeaponForThrow())
	{
		return false;
	}
	CharacterOwner->SetCombatWeaponWorldTransform(ThirdAttackThrowStart);
	ThirdAttackThrowPhase = ERoverThirdAttackThrowPhase::Outbound;
	ThirdAttackThrowPhaseElapsed = 0.0f;
	ThirdAttackThrowPhaseAlpha = 0.0f;
	RefreshThirdAttackWeaponThrowTrace();
	return true;
}

void URoverCombatComponent::BeginThirdAttackWeaponReturn()
{
	if (!CharacterOwner || !CharacterOwner->GetCombatWeapon())
	{
		EndThirdAttackWeaponThrow(true);
		return;
	}

	ThirdAttackThrowReturnStart = CharacterOwner->GetCombatWeapon()->GetComponentTransform();
	ThirdAttackThrowPhase = ERoverThirdAttackThrowPhase::Returning;
	ThirdAttackThrowPhaseElapsed = 0.0f;
	ThirdAttackThrowPhaseAlpha = 0.0f;
	RefreshThirdAttackWeaponThrowTrace();
}

void URoverCombatComponent::UpdateThirdAttackWeaponThrow(const float DeltaTime)
{
	if (ThirdAttackThrowPhase == ERoverThirdAttackThrowPhase::Inactive)
	{
		return;
	}
	if (!CharacterOwner || ThirdAttackThrowRequestId != ActiveAttackRequestId || CurrentComboIndex != 3)
	{
		EndThirdAttackWeaponThrow(true);
		return;
	}

	const FRoverCombatSettings& Settings = GetSettings();
	float RemainingDelta = FMath::Max(0.0f, DeltaTime);
	for (int32 TransitionGuard = 0;
		TransitionGuard < 5 && ThirdAttackThrowPhase != ERoverThirdAttackThrowPhase::Inactive;
		++TransitionGuard)
	{
		if (ThirdAttackThrowPhase == ERoverThirdAttackThrowPhase::Waiting)
		{
			const float Duration = FMath::Max(0.0f, Settings.ThirdAttackThrowStartDelay);
			const float Step = FMath::Min(RemainingDelta, FMath::Max(0.0f, Duration - ThirdAttackThrowPhaseElapsed));
			ThirdAttackThrowPhaseElapsed += Step;
			RemainingDelta -= Step;
			ThirdAttackThrowPhaseAlpha = Duration > KINDA_SMALL_NUMBER
				? FMath::Clamp(ThirdAttackThrowPhaseElapsed / Duration, 0.0f, 1.0f)
				: 1.0f;
			if (ThirdAttackThrowPhaseAlpha >= 1.0f)
			{
				if (!BeginThirdAttackWeaponOutbound())
				{
					EndThirdAttackWeaponThrow(true);
				}
				continue;
			}
			break;
		}

		if (ThirdAttackThrowPhase == ERoverThirdAttackThrowPhase::Outbound)
		{
			const float Duration = FMath::Max(0.01f, Settings.ThirdAttackThrowOutboundDuration);
			const float Step = FMath::Min(RemainingDelta, FMath::Max(0.0f, Duration - ThirdAttackThrowPhaseElapsed));
			ThirdAttackThrowPhaseElapsed += Step;
			RemainingDelta -= Step;
			ThirdAttackThrowPhaseAlpha = FMath::Clamp(ThirdAttackThrowPhaseElapsed / Duration, 0.0f, 1.0f);
			CharacterOwner->SetCombatWeaponWorldTransform(BlendThrowTransform(
				ThirdAttackThrowStart,
				ThirdAttackThrowAnchor,
				ThirdAttackThrowPhaseAlpha));
			if (ThirdAttackThrowPhaseAlpha >= 1.0f)
			{
				ThirdAttackThrowPhase = ERoverThirdAttackThrowPhase::Spinning;
				ThirdAttackThrowPhaseElapsed = 0.0f;
				ThirdAttackThrowPhaseAlpha = 0.0f;
				RefreshThirdAttackWeaponThrowTrace();
				continue;
			}
			break;
		}

		if (ThirdAttackThrowPhase == ERoverThirdAttackThrowPhase::Spinning)
		{
			const float Duration = FMath::Max(0.0f, Settings.ThirdAttackThrowSpinDuration);
			if (Duration <= KINDA_SMALL_NUMBER)
			{
				BeginThirdAttackWeaponReturn();
				continue;
			}
			const float Step = FMath::Min(RemainingDelta, FMath::Max(0.0f, Duration - ThirdAttackThrowPhaseElapsed));
			ThirdAttackThrowPhaseElapsed += Step;
			RemainingDelta -= Step;
			ThirdAttackThrowSpinDegrees += Settings.ThirdAttackThrowSpinDegreesPerSecond * Step;
			ThirdAttackThrowPhaseAlpha = FMath::Clamp(ThirdAttackThrowPhaseElapsed / Duration, 0.0f, 1.0f);
			const FQuat SpinRotation(
				ThirdAttackThrowSpinAxisWorld,
				FMath::DegreesToRadians(ThirdAttackThrowSpinDegrees));
			FTransform SpinTransform = ThirdAttackThrowAnchor;
			SpinTransform.SetRotation((SpinRotation * ThirdAttackThrowAnchor.GetRotation()).GetNormalized());
			CharacterOwner->SetCombatWeaponWorldTransform(SpinTransform);
			if (ThirdAttackThrowPhaseAlpha >= 1.0f)
			{
				BeginThirdAttackWeaponReturn();
				continue;
			}
			break;
		}

		if (ThirdAttackThrowPhase == ERoverThirdAttackThrowPhase::Returning)
		{
			FTransform LeftHandTarget;
			if (!CharacterOwner->GetCombatWeaponAttachmentWorldTransform(
				ERoverWeaponHand::Left,
				LeftHandTarget))
			{
				EndThirdAttackWeaponThrow(true);
				break;
			}

			const float Duration = FMath::Max(0.01f, Settings.ThirdAttackThrowReturnDuration);
			const float Step = FMath::Min(RemainingDelta, FMath::Max(0.0f, Duration - ThirdAttackThrowPhaseElapsed));
			ThirdAttackThrowPhaseElapsed += Step;
			RemainingDelta -= Step;
			ThirdAttackThrowPhaseAlpha = FMath::Clamp(ThirdAttackThrowPhaseElapsed / Duration, 0.0f, 1.0f);
			const float EasedAlpha = SmoothStepAlpha(ThirdAttackThrowPhaseAlpha);
			FTransform ReturnTransform = BlendThrowTransform(
				ThirdAttackThrowReturnStart,
				LeftHandTarget,
				ThirdAttackThrowPhaseAlpha);
			const FQuat ContinuedSpin(
				ThirdAttackThrowSpinAxisWorld,
				FMath::DegreesToRadians(
					Settings.ThirdAttackThrowSpinDegreesPerSecond * ThirdAttackThrowPhaseElapsed));
			const FQuat SpinningRotation =
				(ContinuedSpin * ThirdAttackThrowReturnStart.GetRotation()).GetNormalized();
			ReturnTransform.SetRotation(FQuat::Slerp(
				SpinningRotation,
				LeftHandTarget.GetRotation(),
				EasedAlpha).GetNormalized());
			CharacterOwner->SetCombatWeaponWorldTransform(ReturnTransform);
			if (ThirdAttackThrowPhaseAlpha >= 1.0f)
			{
				if (bWeaponTraceActive)
				{
					PerformWeaponTrace();
				}
				EndThirdAttackWeaponThrow(true, ERoverWeaponHand::Left);
				continue;
			}
			break;
		}
	}

	if (ThirdAttackThrowPhase != ERoverThirdAttackThrowPhase::Inactive &&
		CVarDrawAttackTrace.GetValueOnGameThread())
	{
		if (const FRoverAttackDefinition* Definition = GetAttackDefinition(CurrentComboIndex))
		{
			const float Duration = FMath::Max(0.01f, CVarDrawAttackTraceDuration.GetValueOnGameThread());
			DrawDebugSphere(
				GetWorld(),
				ThirdAttackThrowAnchor.GetLocation(),
				ResolveActiveTraceRadius(*Definition),
				16,
				FColor::Yellow,
				false,
				Duration,
				0,
				1.5f);
		}
	}
}

void URoverCombatComponent::EndThirdAttackWeaponThrow(
	const bool bSnapToHand,
	const ERoverWeaponHand TargetHand)
{
	const bool bWasActive = ThirdAttackThrowPhase != ERoverThirdAttackThrowPhase::Inactive;
	if (bWasActive)
	{
		DisableWeaponTrace();
	}
	ThirdAttackThrowPhase = ERoverThirdAttackThrowPhase::Inactive;
	ThirdAttackThrowRequestId = 0;
	ThirdAttackThrowPhaseElapsed = 0.0f;
	ThirdAttackThrowPhaseAlpha = 0.0f;
	ThirdAttackThrowSpinDegrees = 0.0f;
	ThirdAttackThrowStart = FTransform::Identity;
	ThirdAttackThrowAnchor = FTransform::Identity;
	ThirdAttackThrowReturnStart = FTransform::Identity;
	if (bWasActive && bSnapToHand && CharacterOwner)
	{
		CharacterOwner->RestoreCombatWeaponAttachment(TargetHand);
	}
}

bool URoverCombatComponent::ShouldTraceThirdAttackWeaponThrow() const
{
	const FRoverCombatSettings& Settings = GetSettings();
	switch (ThirdAttackThrowPhase)
	{
	case ERoverThirdAttackThrowPhase::Outbound:
		return Settings.bThirdAttackThrowCollisionOutbound;
	case ERoverThirdAttackThrowPhase::Spinning:
		return Settings.bThirdAttackThrowCollisionSpinning;
	case ERoverThirdAttackThrowPhase::Returning:
		return Settings.bThirdAttackThrowCollisionReturning;
	default:
		return false;
	}
}

void URoverCombatComponent::RefreshThirdAttackWeaponThrowTrace()
{
	if (ShouldTraceThirdAttackWeaponThrow())
	{
		if (!bWeaponTraceActive)
		{
			EnableWeaponTrace();
		}
		return;
	}
	if (bWeaponTraceActive)
	{
		PerformWeaponTrace();
	}
	DisableWeaponTrace();
}

float URoverCombatComponent::ResolveActiveTraceRadius(const FRoverAttackDefinition& Definition) const
{
	return IsThirdAttackWeaponThrowActive()
		? FMath::Max(0.0f, GetSettings().ThirdAttackThrowTraceRadius)
		: FMath::Max(0.0f, Definition.TraceRadius);
}

int32 URoverCombatComponent::ResolveActiveTraceSampleCount(const FRoverAttackDefinition& Definition) const
{
	return IsThirdAttackWeaponThrowActive()
		? FMath::Clamp(GetSettings().ThirdAttackThrowTraceSampleCount, 2, 24)
		: FMath::Clamp(Definition.TraceSampleCount, 2, 16);
}

float URoverCombatComponent::ResolveActiveTraceSubstepDistance(const FRoverAttackDefinition& Definition) const
{
	return IsThirdAttackWeaponThrowActive()
		? FMath::Max(1.0f, GetSettings().ThirdAttackThrowTraceSubstepDistance)
		: FMath::Max(1.0f, Definition.TraceSubstepDistance);
}

int32 URoverCombatComponent::ResolveActiveMaxTraceSubsteps(const FRoverAttackDefinition& Definition) const
{
	return IsThirdAttackWeaponThrowActive()
		? FMath::Clamp(GetSettings().ThirdAttackThrowMaxTraceSubsteps, 1, 24)
		: FMath::Clamp(Definition.MaxTraceSubsteps, 1, 16);
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

	const FRoverAttackDefinition* Definition = GetActiveAttackDefinition();
	if (!Definition)
	{
		DisableWeaponTrace();
		return;
	}

	const FVector BaseTravel = CurrentBase - PreviousTraceBase;
	const FVector TipTravel = CurrentTip - PreviousTraceTip;
	const float MaxEndpointTravel = FMath::Max(BaseTravel.Size(), TipTravel.Size());
	const float SubstepDistance = ResolveActiveTraceSubstepDistance(*Definition);
	const int32 SubstepCount = FMath::Clamp(
		FMath::CeilToInt(MaxEndpointTravel / SubstepDistance),
		1,
		ResolveActiveMaxTraceSubsteps(*Definition));
	const int32 BladeSampleCount = ResolveActiveTraceSampleCount(*Definition);

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
		const float Radius = ResolveActiveTraceRadius(*Definition);
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
	const FRoverAttackDefinition* Definition = GetActiveAttackDefinition();
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
		FCollisionShape::MakeSphere(ResolveActiveTraceRadius(*Definition)),
		QueryParams);

	if (CVarDrawAttackTrace.GetValueOnGameThread())
	{
		const float Duration = FMath::Max(0.01f, CVarDrawAttackTraceDuration.GetValueOnGameThread());
		const float Radius = ResolveActiveTraceRadius(*Definition);
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
