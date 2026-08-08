#include "RoverLocomotionComponent.h"

#include "RoverRootMotionSourceNames.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"

namespace
{
bool IsCharacterOnSimulatedBase(
	const ACharacter* Character,
	const UCharacterMovementComponent* Movement)
{
	if (!Character || !Movement)
	{
		return false;
	}

	const FBasedMovementInfo& BasedMovement = Character->GetBasedMovement();
	if (MovementBaseUtility::IsSimulatedBase(&BasedMovement.MovementBaseInterfaceData))
	{
		return true;
	}

	const UPrimitiveComponent* MovementBase =
		Cast<UPrimitiveComponent>(Character->GetMovementBaseObject());
	return IsValid(MovementBase) && MovementBase->IsSimulatingPhysics();
}
}

URoverLocomotionComponent::URoverLocomotionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void URoverLocomotionComponent::BeginPlay()
{
	Super::BeginPlay();

	CharacterOwner = Cast<ACharacter>(GetOwner());
	if (!CharacterOwner)
	{
		UE_LOG(LogTemp, Error, TEXT("RoverLocomotionComponent requires an ACharacter owner."));
		SetComponentTickEnabled(false);
		return;
	}

	CharacterMovement = CharacterOwner->GetCharacterMovement();
	if (CharacterMovement)
	{
		CharacterMovement->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
	}
	ApplySettings();
	UpdateLocomotionState();
}

void URoverLocomotionComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!CharacterOwner || !CharacterMovement)
	{
		return;
	}
	UpdateCombatPhysicsPushRestore(DeltaTime);

	if (CharacterMovement->IsFalling() && CharacterOwner->GetVelocity().Z < 0.0f)
	{
		CachedDownwardSpeed = FMath::Max(CachedDownwardSpeed, -CharacterOwner->GetVelocity().Z);
	}

	InputLockRemaining = FMath::Max(0.0f, InputLockRemaining - DeltaTime);
	LandingStateRemaining = FMath::Max(0.0f, LandingStateRemaining - DeltaTime);
	if (LandingStateRemaining <= 0.0f)
	{
		LandingType = ERoverLandingType::None;
	}

	const FRoverMovementSettings& Settings = GetSettings();
	const float TargetTurnRate = CharacterMovement->IsFalling() ? Settings.AirTurnRate : Settings.RotationRate;
	CharacterMovement->RotationRate = FRotator(0.0f, TargetTurnRate, 0.0f);

	if (!CharacterMovement->IsMovingOnGround() && (bGroundTurnPending || bGroundTurnActive))
	{
		CancelGroundTurn();
	}
	if (!CharacterMovement->IsMovingOnGround() && (bMoveStopPending || bMoveStopActive))
	{
		CancelMoveStop();
	}
	UpdateGroundTurnWatchdog(DeltaTime);
	UpdateActiveRunTurnback();
	UpdateMoveStopWatchdog(DeltaTime);
	UpdateLocomotionState();
	UpdateGait();
}

const FRoverMovementSettings& URoverLocomotionComponent::GetSettings() const
{
	return MovementConfig ? MovementConfig->Settings : FallbackSettings;
}

void URoverLocomotionComponent::ApplySettings()
{
	if (!CharacterOwner || !CharacterMovement)
	{
		return;
	}

	const FRoverMovementSettings& Settings = GetSettings();
	CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(Settings.CapsuleRadius, Settings.CapsuleHalfHeight);
	CharacterMovement->MaxWalkSpeed = Settings.RunSpeed;
	CharacterMovement->MaxAcceleration = Settings.MaxAcceleration;
	CharacterMovement->BrakingDecelerationWalking = Settings.BrakingDeceleration;
	CharacterMovement->GroundFriction = Settings.GroundFriction;
	CharacterMovement->JumpZVelocity = Settings.JumpZVelocity;
	CharacterMovement->GravityScale = Settings.GravityScale;
	CharacterMovement->AirControl = Settings.AirControl;
	CharacterMovement->RotationRate = FRotator(0.0f, Settings.RotationRate, 0.0f);
	CharacterMovement->bEnablePhysicsInteraction = Settings.bEnablePhysicsInteraction;
	CharacterMovement->bPushForceScaledToMass = Settings.bPhysicsInteractionPushForceScaledToMass;
	CharacterMovement->bTouchForceScaledToMass = Settings.bPhysicsInteractionTouchForceScaledToMass;
	CharacterMovement->bScalePushForceToVelocity = Settings.bPhysicsInteractionScalePushForceToVelocity;
	CharacterMovement->Mass = Settings.PhysicsInteractionCharacterMassKg;
	CharacterMovement->StandingDownwardForceScale = Settings.PhysicsInteractionStandingDownwardForceScale;
	CharacterMovement->InitialPushForceFactor = Settings.PhysicsInteractionInitialPushForceFactor;
	CharacterMovement->PushForceFactor = Settings.PhysicsInteractionPushForceFactor;
	CharacterMovement->TouchForceFactor = Settings.PhysicsInteractionTouchForceFactor;
	CharacterMovement->MinTouchForce = Settings.PhysicsInteractionMinTouchForce;
	CharacterMovement->MaxTouchForce = Settings.PhysicsInteractionMaxTouchForce;
	CharacterMovement->RepulsionForce = Settings.PhysicsInteractionRepulsionForce;
}

void URoverLocomotionComponent::SetMoveInput(const FVector2D& Input, const FVector& WorldDirection)
{
	const float InputMagnitude = FMath::Min(Input.Size(), 1.0f);
	const float DeadZone = FMath::Clamp(GetSettings().InputDeadZone, 0.0f, 1.0f);
	if (InputMagnitude <= DeadZone)
	{
		const bool bRequestReleaseStop = ShouldRequestReleaseStop();
		MoveInput = FVector2D::ZeroVector;
		MoveWorldDirection = FVector::ZeroVector;
		if (bGroundTurnPending)
		{
			CancelGroundTurn();
		}
		else if (bGroundTurnActive)
		{
			bGroundTurnResumeMovement = false;
		}
		if (bMoveStopPending || bMoveStopActive)
		{
			bMoveStopResumeMovement = false;
		}
		else if (bRequestReleaseStop)
		{
			RequestReleaseStop();
		}
		bGroundTurnArmed = true;
		return;
	}

	// Landing roll may keep its full visual recovery when the player is idle, but
	// held movement must leave the state as soon as its input lock expires. Keeping
	// LandRoll active after accepting input lets its root motion override walking
	// and creates a visible pause before Grounded resumes.
	const bool bCanInterruptLanding =
		LandingType == ERoverLandingType::Light ||
		LandingType == ERoverLandingType::Roll;
	if (bCanInterruptLanding && InputLockRemaining <= 0.0f)
	{
		LandingType = ERoverLandingType::None;
		LandingStateRemaining = 0.0f;
	}

	const float RescaledMagnitude = (InputMagnitude - DeadZone) / (1.0f - DeadZone);
	MoveInput = Input.GetSafeNormal() * RescaledMagnitude;
	MoveWorldDirection = WorldDirection.GetSafeNormal2D();
	if (bMoveStopPending || bMoveStopActive)
	{
		const float MinimumCompatibleDot = FMath::Cos(FMath::DegreesToRadians(
			FMath::Clamp(GetSettings().GroundTurnRearmAngle, 0.0f, 180.0f)));
		bMoveStopResumeMovement = !MoveWorldDirection.IsNearlyZero() &&
			FVector::DotProduct(MoveWorldDirection, MoveStopDesiredDirection) >= MinimumCompatibleDot;
		if (bMoveStopResumeMovement)
		{
			MoveStopDesiredDirection = MoveWorldDirection;
			return;
		}
		else
		{
			if (MoveStopGait == ERoverGait::Walk)
			{
				bGroundTurnArmed = false;
			}
			CancelMoveStop();
		}
	}
	if (bGroundTurnActive)
	{
		bGroundTurnResumeMovement = IsGroundTurnInputCompatible();
		return;
	}
	UpdateGroundTurnRequest();
}

void URoverLocomotionComponent::SetSprintHeld(const bool bHeld)
{
	bSprintHeld = bHeld;
	if (!bSprintHeld &&
		bGroundTurnPending &&
		GroundTurnType == ERoverGroundTurnType::RunTurnback)
	{
		CancelGroundTurn();
	}
}

void URoverLocomotionComponent::SetCrouchHeld(const bool bHeld)
{
	bCrouchHeld = bHeld;
}

bool URoverLocomotionComponent::TryBeginCombatMovementRestriction(const int32 RequestId)
{
	if (RequestId <= 0 || !CharacterOwner || !CharacterMovement ||
		!CharacterMovement->IsMovingOnGround() ||
		LandingType == ERoverLandingType::Heavy ||
		LandingType == ERoverLandingType::Roll ||
		InputLockRemaining > 0.0f)
	{
		return false;
	}

	CancelGroundTurn();
	CancelMoveStop();
	if (LandingType == ERoverLandingType::Light)
	{
		LandingType = ERoverLandingType::None;
		LandingStateRemaining = 0.0f;
	}
	CombatMovementRestrictionRequestId = RequestId;
	if (IsCharacterOnSimulatedBase(CharacterOwner, CharacterMovement))
	{
		ApplyCombatPhysicsPushScale(RequestId);
	}
	return true;
}

bool URoverLocomotionComponent::TryBeginAirCombatMovementRestriction(
	const int32 RequestId,
	const float HorizontalVelocityScale)
{
	if (RequestId <= 0 || !CharacterOwner || !CharacterMovement ||
		!CharacterMovement->IsFalling() || InputLockRemaining > 0.0f ||
		CombatMovementRestrictionRequestId != 0)
	{
		return false;
	}

	CancelGroundTurn();
	CancelMoveStop();
	CombatMovementRestrictionRequestId = RequestId;
	const float RetainedHorizontalScale = FMath::Clamp(HorizontalVelocityScale, 0.0f, 1.0f);
	CharacterMovement->Velocity.X *= RetainedHorizontalScale;
	CharacterMovement->Velocity.Y *= RetainedHorizontalScale;
	return true;
}

bool URoverLocomotionComponent::BeginAirCombatAscent(
	const int32 RequestId,
	const float AscentHeight,
	const float AscentDuration)
{
	if (RequestId <= 0 || RequestId != CombatMovementRestrictionRequestId ||
		!CharacterMovement || !CharacterMovement->IsFalling())
	{
		return false;
	}

	const float Duration = FMath::Max(AscentDuration, UE_SMALL_NUMBER);
	const float Height = FMath::Max(0.0f, AscentHeight);
	if (Height <= UE_SMALL_NUMBER)
	{
		CharacterMovement->Velocity.Z = 0.0f;
		return true;
	}

	// Solve s = v0*t + 0.5*g*t^2 so the configured displacement is reached
	// when the animation's apex notify fires.
	const float GravityZ = CharacterMovement->GetGravityZ();
	CharacterMovement->Velocity.Z = FMath::Max(
		0.0f,
		(Height - 0.5f * GravityZ * Duration * Duration) / Duration);
	return true;
}

bool URoverLocomotionComponent::BeginAirCombatDescent(
	const int32 RequestId,
	const float DescentSpeed)
{
	if (RequestId <= 0 || RequestId != CombatMovementRestrictionRequestId ||
		!CharacterMovement || !CharacterMovement->IsFalling())
	{
		return false;
	}

	CharacterMovement->Velocity.Z = -FMath::Max(0.0f, DescentSpeed);
	return true;
}

bool URoverLocomotionComponent::TransferCombatMovementRestriction(
	const int32 PreviousRequestId,
	const int32 NewRequestId)
{
	if (PreviousRequestId <= 0 || NewRequestId <= 0 ||
		CombatMovementRestrictionRequestId != PreviousRequestId)
	{
		return false;
	}

	CombatMovementRestrictionRequestId = NewRequestId;
	if (CombatPhysicsPushScaleRequestId == PreviousRequestId)
	{
		CombatPhysicsPushScaleRequestId = NewRequestId;
	}
	return true;
}

void URoverLocomotionComponent::EndCombatMovementRestriction(
	const int32 RequestId,
	const bool bRestorePhysicsPush)
{
	if (RequestId > 0 && RequestId == CombatMovementRestrictionRequestId)
	{
		CancelCombatAttackAdvance(RequestId);
		CombatMovementRestrictionRequestId = 0;
	}
	if (bRestorePhysicsPush)
	{
		RestoreCombatPhysicsPushScale(RequestId);
	}
}

bool URoverLocomotionComponent::StartCombatAttackAdvance(
	const int32 RequestId,
	const float Distance,
	const float Duration)
{
	if (RequestId <= 0 || RequestId != CombatMovementRestrictionRequestId ||
		!CharacterOwner || !CharacterMovement || !CharacterMovement->IsMovingOnGround())
	{
		return false;
	}

	const int32 PreviousAttackMovementRequestId = CombatAttackAdvanceRequestId > 0
		? CombatAttackAdvanceRequestId
		: CombatPhysicsPushScaleRequestId;
	CancelCombatAttackAdvance(PreviousAttackMovementRequestId);
	float ResolvedDistance = Distance;
	// The animated start notify can arrive after a one-frame movement-base transition.
	// Preserve the classification captured when this attack request was accepted.
	const bool bIsOnSimulatedBase =
		CombatPhysicsPushScaleRequestId == RequestId ||
		IsCharacterOnSimulatedBase(CharacterOwner, CharacterMovement);
	if (bIsOnSimulatedBase)
	{
		ApplyCombatPhysicsPushScale(RequestId);
		ResolvedDistance *= FMath::Clamp(
			GetSettings().AttackAdvanceScaleOnSimulatedBase,
			0.0f,
			1.0f);
	}
	if (Duration <= UE_KINDA_SMALL_NUMBER)
	{
		return true;
	}
	if (FMath::Abs(ResolvedDistance) <= UE_KINDA_SMALL_NUMBER)
	{
		return true;
	}

	const FVector StartLocation = CharacterOwner->GetActorLocation();
	const FVector AdvanceDirection = CharacterOwner->GetActorForwardVector().GetSafeNormal2D();
	if (AdvanceDirection.IsNearlyZero())
	{
		return false;
	}

	TSharedPtr<FRootMotionSource_MoveToForce> MoveToSource = MakeShared<FRootMotionSource_MoveToForce>();
	MoveToSource->InstanceName = RoverRootMotionSourceNames::CombatAttackAdvance();
	MoveToSource->Priority = 1000;
	MoveToSource->AccumulateMode = ERootMotionAccumulateMode::Override;
	MoveToSource->Duration = FMath::Max(0.05f, Duration);
	MoveToSource->StartLocation = StartLocation;
	MoveToSource->TargetLocation = StartLocation + AdvanceDirection * ResolvedDistance;
	MoveToSource->bRestrictSpeedToExpected = true;
	MoveToSource->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::ClampVelocity;
	MoveToSource->FinishVelocityParams.ClampVelocity = GetMaxSpeedForGait(Gait);

	const uint16 SourceId = CharacterMovement->ApplyRootMotionSource(MoveToSource);
	if (SourceId == static_cast<uint16>(ERootMotionSourceID::Invalid))
	{
		return false;
	}

	CombatAttackAdvanceRequestId = RequestId;
	CombatAttackRootMotionSourceId = SourceId;
	return true;
}

void URoverLocomotionComponent::CancelCombatAttackAdvance(const int32 RequestId)
{
	if (RequestId <= 0 ||
		(RequestId != CombatAttackAdvanceRequestId &&
			RequestId != CombatPhysicsPushScaleRequestId))
	{
		return;
	}
	if (RequestId == CombatAttackAdvanceRequestId && CharacterMovement &&
		CombatAttackRootMotionSourceId != static_cast<uint16>(ERootMotionSourceID::Invalid))
	{
		CharacterMovement->RemoveRootMotionSourceByID(CombatAttackRootMotionSourceId);
	}
	if (RequestId == CombatAttackAdvanceRequestId)
	{
		CombatAttackAdvanceRequestId = 0;
		CombatAttackRootMotionSourceId = static_cast<uint16>(ERootMotionSourceID::Invalid);
	}
}

void URoverLocomotionComponent::ApplyCombatPhysicsPushScale(const int32 RequestId)
{
	if (RequestId <= 0 || !CharacterMovement)
	{
		return;
	}
	if (RequestId == CombatPhysicsPushScaleRequestId)
	{
		return;
	}

	const bool bHasCachedOriginalValues =
		CombatPhysicsPushScaleRequestId > 0 || bCombatPhysicsRestoreActive;
	if (!bHasCachedOriginalValues)
	{
		CachedCombatInitialPushForceFactor = CharacterMovement->InitialPushForceFactor;
		CachedCombatPushForceFactor = CharacterMovement->PushForceFactor;
		CachedCombatStandingDownwardForceScale = CharacterMovement->StandingDownwardForceScale;
	}
	bCombatPhysicsRestoreActive = false;
	CombatPhysicsRestoreElapsed = 0.0f;
	const float PushScale = FMath::Clamp(
		GetSettings().AttackPhysicsPushScaleOnSimulatedBase,
		0.0f,
		1.0f);
	const float StandingLoadScale = FMath::Clamp(
		GetSettings().AttackStandingDownwardForceScaleOnSimulatedBase,
		0.0f,
		1.0f);
	CharacterMovement->InitialPushForceFactor = CachedCombatInitialPushForceFactor * PushScale;
	CharacterMovement->PushForceFactor = CachedCombatPushForceFactor * PushScale;
	CharacterMovement->StandingDownwardForceScale =
		CachedCombatStandingDownwardForceScale * StandingLoadScale;
	CombatPhysicsPushScaleRequestId = RequestId;
}

void URoverLocomotionComponent::RestoreCombatPhysicsPushScale(const int32 RequestId)
{
	if (RequestId <= 0 || RequestId != CombatPhysicsPushScaleRequestId)
	{
		return;
	}

	if (CharacterMovement)
	{
		const float RestoreDuration = FMath::Max(
			0.0f,
			GetSettings().AttackPhysicsRestoreDurationOnSimulatedBase);
		if (RestoreDuration > UE_KINDA_SMALL_NUMBER)
		{
			CombatPhysicsRestoreStartInitialPushForceFactor =
				CharacterMovement->InitialPushForceFactor;
			CombatPhysicsRestoreStartPushForceFactor = CharacterMovement->PushForceFactor;
			CombatPhysicsRestoreStartStandingDownwardForceScale =
				CharacterMovement->StandingDownwardForceScale;
			CombatPhysicsRestoreElapsed = 0.0f;
			bCombatPhysicsRestoreActive = true;
		}
		else
		{
			CharacterMovement->InitialPushForceFactor = CachedCombatInitialPushForceFactor;
			CharacterMovement->PushForceFactor = CachedCombatPushForceFactor;
			CharacterMovement->StandingDownwardForceScale = CachedCombatStandingDownwardForceScale;
			ClearCombatPhysicsPushRestoreState();
		}
	}
	CombatPhysicsPushScaleRequestId = 0;
}

void URoverLocomotionComponent::UpdateCombatPhysicsPushRestore(const float DeltaTime)
{
	if (!bCombatPhysicsRestoreActive || !CharacterMovement)
	{
		return;
	}

	const float RestoreDuration = FMath::Max(
		UE_KINDA_SMALL_NUMBER,
		GetSettings().AttackPhysicsRestoreDurationOnSimulatedBase);
	CombatPhysicsRestoreElapsed += FMath::Max(0.0f, DeltaTime);
	const float LinearAlpha = FMath::Clamp(CombatPhysicsRestoreElapsed / RestoreDuration, 0.0f, 1.0f);
	const float SmoothAlpha = FMath::SmoothStep(0.0f, 1.0f, LinearAlpha);
	CharacterMovement->InitialPushForceFactor = FMath::Lerp(
		CombatPhysicsRestoreStartInitialPushForceFactor,
		CachedCombatInitialPushForceFactor,
		SmoothAlpha);
	CharacterMovement->PushForceFactor = FMath::Lerp(
		CombatPhysicsRestoreStartPushForceFactor,
		CachedCombatPushForceFactor,
		SmoothAlpha);
	CharacterMovement->StandingDownwardForceScale = FMath::Lerp(
		CombatPhysicsRestoreStartStandingDownwardForceScale,
		CachedCombatStandingDownwardForceScale,
		SmoothAlpha);
	if (LinearAlpha >= 1.0f)
	{
		ClearCombatPhysicsPushRestoreState();
	}
}

void URoverLocomotionComponent::ClearCombatPhysicsPushRestoreState()
{
	bCombatPhysicsRestoreActive = false;
	CombatPhysicsRestoreElapsed = 0.0f;
	CombatPhysicsRestoreStartInitialPushForceFactor = 0.0f;
	CombatPhysicsRestoreStartPushForceFactor = 0.0f;
	CombatPhysicsRestoreStartStandingDownwardForceScale = 0.0f;
	CachedCombatInitialPushForceFactor = 0.0f;
	CachedCombatPushForceFactor = 0.0f;
	CachedCombatStandingDownwardForceScale = 0.0f;
}

bool URoverLocomotionComponent::IsCombatAttackAdvanceActive() const
{
	return CharacterMovement &&
		CombatAttackRootMotionSourceId != static_cast<uint16>(ERootMotionSourceID::Invalid) &&
		CharacterMovement->GetRootMotionSourceByID(CombatAttackRootMotionSourceId).IsValid();
}

bool URoverLocomotionComponent::HasActiveAnimationRootMotion() const
{
	return CharacterMovement && CharacterMovement->HasAnimRootMotion();
}

bool URoverLocomotionComponent::TryJump()
{
	if (!CharacterOwner || !CharacterMovement || InputLockRemaining > 0.0f ||
		CombatMovementRestrictionRequestId != 0)
	{
		return false;
	}

	const FRoverMovementSettings& Settings = GetSettings();
	if (CharacterMovement->IsMovingOnGround())
	{
		CancelMoveStop();
		if (!CharacterOwner->CanJump())
		{
			return false;
		}

		CancelGroundTurn();
		CharacterMovement->JumpZVelocity = Settings.JumpZVelocity;
		CharacterOwner->Jump();
		return true;
	}

	if (CharacterMovement->IsFalling() && !bSecondJumpUsed)
	{
		CancelGroundTurn();
		CancelMoveStop();
		bSecondJumpMovedBackward = !MoveWorldDirection.IsNearlyZero() &&
			FVector::DotProduct(CharacterOwner->GetActorForwardVector(), MoveWorldDirection) < 0.0f;
		const FVector HorizontalBoost = MoveWorldDirection * Settings.SecondJumpHorizontalBoost;
		CharacterOwner->LaunchCharacter(
			FVector(HorizontalBoost.X, HorizontalBoost.Y, Settings.SecondJumpZVelocity),
			false,
			true);
		bSecondJumpUsed = true;
		OnSecondJump.Broadcast();
		return true;
	}

	return false;
}

void URoverLocomotionComponent::HandleGroundJumped()
{
	if (!CharacterOwner || !CharacterMovement)
	{
		return;
	}

	const FRoverMovementSettings& Settings = GetSettings();
	bSecondJumpUsed = false;
	bGroundJumpStartedFromRun = CharacterOwner->GetVelocity().Size2D() >= Settings.JumpRunSpeedThreshold;
	bGroundJumpUsedLeftFoot = bNextGroundJumpUsesLeftFoot;
	bNextGroundJumpUsesLeftFoot = !bNextGroundJumpUsesLeftFoot;
	GroundJumpRequestId = GroundJumpRequestId == MAX_int32 ? 1 : GroundJumpRequestId + 1;
	UpdateLocomotionState();
}

void URoverLocomotionComponent::StopJump()
{
	if (CharacterOwner)
	{
		CharacterOwner->StopJumping();
	}
}

void URoverLocomotionComponent::HandleMovementModeChanged()
{
	if (!CharacterMovement)
	{
		return;
	}

	if (CharacterMovement->IsMovingOnGround())
	{
		bSecondJumpUsed = false;
		CachedDownwardSpeed = 0.0f;
	}
	else
	{
		CancelGroundTurn();
		CancelMoveStop();
	}

	UpdateLocomotionState();
}

void URoverLocomotionComponent::HandleLanded(
	const float ImpactSpeed,
	const bool bSuppressLandingAnimation)
{
	if (!CharacterMovement)
	{
		return;
	}

	const FRoverMovementSettings& Settings = GetSettings();
	const float ResolvedImpactSpeed = FMath::Max(ImpactSpeed, CachedDownwardSpeed);
	CachedDownwardSpeed = 0.0f;
	bSecondJumpUsed = false;
	InputLockRemaining = 0.0f;
	LandingStateRemaining = 0.0f;
	if (bSuppressLandingAnimation)
	{
		LandingType = ERoverLandingType::None;
		OnLanded.Broadcast(LandingType, ResolvedImpactSpeed);
		UpdateLocomotionState();
		return;
	}

	if (bCrouchHeld || ResolvedImpactSpeed > Settings.HeavyLandingMaxSpeed)
	{
		LandingType = ERoverLandingType::Roll;
		InputLockRemaining = Settings.RollLandingLockTime;
		LandingStateRemaining = FMath::Max(
			Settings.RollLandingLockTime,
			Settings.RollLandingStateTime);
	}
	else if (ResolvedImpactSpeed >= Settings.LightLandingMaxSpeed)
	{
		LandingType = ERoverLandingType::Heavy;
		InputLockRemaining = Settings.HeavyLandingLockTime;
		LandingStateRemaining = Settings.HeavyLandingLockTime;
	}
	else if (!MoveInput.IsNearlyZero())
	{
		LandingType = ERoverLandingType::None;
	}
	else
	{
		LandingType = ERoverLandingType::Light;
		LandingStateRemaining = Settings.LightLandingStateTime;
	}

	OnLanded.Broadcast(LandingType, ResolvedImpactSpeed);
	UpdateLocomotionState();
}

void URoverLocomotionComponent::UpdateLocomotionState()
{
	if (!CharacterMovement)
	{
		return;
	}

	ERoverLocomotionState NewState = ERoverLocomotionState::Grounded;
	if (CharacterMovement->MovementMode == MOVE_Swimming)
	{
		NewState = ERoverLocomotionState::Swimming;
	}
	else if (CharacterMovement->IsFalling())
	{
		NewState = ERoverLocomotionState::Airborne;
	}

	SetLocomotionState(NewState);
}

void URoverLocomotionComponent::UpdateGait()
{
	if (!CharacterMovement || LocomotionState != ERoverLocomotionState::Grounded)
	{
		return;
	}

	const FRoverMovementSettings& Settings = GetSettings();
	const float InputMagnitude = MoveInput.Size();
	const float HorizontalSpeed = CharacterOwner->GetVelocity().Size2D();
	if (bGroundTurnPending || bGroundTurnActive)
	{
		const ERoverGait LockedGait = GroundTurnType == ERoverGroundTurnType::RunTurnback
			? GroundTurnEntryGait
			: ERoverGait::Idle;
		SetGait(LockedGait);
		CharacterMovement->MaxWalkSpeed = GetMaxSpeedForGait(LockedGait);
		return;
	}
	if (bMoveStopPending || bMoveStopActive)
	{
		SetGait(MoveStopGait);
		CharacterMovement->MaxWalkSpeed = GetMaxSpeedForGait(MoveStopGait);
		return;
	}

	if (!MoveInput.IsNearlyZero() && CanAcceptMovementInput())
	{
		ERoverGait DesiredGait = ERoverGait::Run;
		if (bSprintHeld && InputMagnitude >= Settings.SprintInputThreshold)
		{
			DesiredGait = ERoverGait::Sprint;
		}
		else if (Settings.bUseAnalogWalk && InputMagnitude < Settings.AnalogWalkThreshold)
		{
			DesiredGait = ERoverGait::Walk;
		}

		LastMovingGait = DesiredGait;
		SetGait(DesiredGait);
	}
	else if (HorizontalSpeed <= 3.0f)
	{
		SetGait(ERoverGait::Idle);
	}
	else
	{
		SetGait(LastMovingGait);
	}

	CharacterMovement->MaxWalkSpeed = GetMaxSpeedForGait(Gait);
}

void URoverLocomotionComponent::UpdateGroundTurnWatchdog(const float DeltaTime)
{
	if (!bGroundTurnPending && !bGroundTurnActive)
	{
		return;
	}

	GroundTurnStateElapsed += DeltaTime;
	const FRoverMovementSettings& Settings = GetSettings();
	if (bGroundTurnPending && GroundTurnStateElapsed >= FMath::Max(0.1f, Settings.GroundTurnPendingTimeout))
	{
		UE_LOG(LogTemp, Warning, TEXT("Ground turn request %d timed out before its animation state entered."), GroundTurnRequestId);
		CancelGroundTurn();
	}
	else if (bGroundTurnActive && GroundTurnStateElapsed >= FMath::Max(0.1f, Settings.GroundTurnActiveTimeout))
	{
		UE_LOG(LogTemp, Warning, TEXT("Ground turn request %d timed out before its animation state exited; completing from the watchdog."), GroundTurnRequestId);
		CompleteGroundTurn(GroundTurnRequestId);
	}
}

void URoverLocomotionComponent::UpdateActiveRunTurnback()
{
	if (!CharacterOwner || !CharacterMovement || !bGroundTurnActive ||
		GroundTurnType != ERoverGroundTurnType::RunTurnback)
	{
		return;
	}

	FRotator ActorRotation = CharacterOwner->GetActorRotation();
	ActorRotation.Yaw = GroundTurnTargetYaw;
	CharacterOwner->SetActorRotation(ActorRotation);

	// Player input remains locked to the turnback state. Keep the old-direction
	// slide separate from recovery so the animation controls when new-direction
	// movement is allowed to begin.
	CharacterMovement->Velocity.X = 0.0f;
	CharacterMovement->Velocity.Y = 0.0f;
	const FRoverMovementSettings& Settings = GetSettings();
	const float InertiaDistance = FMath::Max(0.0f, Settings.RunTurnbackInertiaDistance);
	const float InertiaDuration = FMath::Max(0.05f, Settings.RunTurnbackInertiaDuration);
	if (!bGroundTurnResumeWindowOpen && !bGroundTurnInertiaBlocked &&
		InertiaDistance > UE_KINDA_SMALL_NUMBER &&
		!GroundTurnInertiaDirection.IsNearlyZero())
	{
		const float InertiaAlpha = FMath::Clamp(GroundTurnStateElapsed / InertiaDuration, 0.0f, 1.0f);
		const float RemainingAlpha = 1.0f - InertiaAlpha;
		const float TargetDistance = InertiaDistance * (1.0f - RemainingAlpha * RemainingAlpha * RemainingAlpha);
		const float DistanceThisFrame = FMath::Max(0.0f, TargetDistance - GroundTurnInertiaDistanceApplied);
		if (DistanceThisFrame > UE_KINDA_SMALL_NUMBER)
		{
			const FVector PreviousLocation = CharacterOwner->GetActorLocation();
			FHitResult SweepHit;
			CharacterOwner->AddActorWorldOffset(
				GroundTurnInertiaDirection * DistanceThisFrame,
				true,
				&SweepHit,
				ETeleportType::None);
			const FVector ActualDelta = CharacterOwner->GetActorLocation() - PreviousLocation;
			CharacterMovement->bForceNextFloorCheck = true;
			GroundTurnInertiaDistanceApplied = FMath::Clamp(
				GroundTurnInertiaDistanceApplied +
					FMath::Max(0.0f, FVector::DotProduct(ActualDelta, GroundTurnInertiaDirection)),
				0.0f,
				InertiaDistance);
			if (SweepHit.bBlockingHit)
			{
				bGroundTurnInertiaBlocked = true;
			}
		}
	}

	if (bGroundTurnResumeWindowOpen &&
		bGroundTurnResumeMovement &&
		!GroundTurnDesiredDirection.IsNearlyZero())
	{
		const float RecoveryElapsed = FMath::Max(
			0.0f,
			GroundTurnStateElapsed - GroundTurnResumeWindowOpenedAt);
		const float MaxRecoverySpeed = GetMaxSpeedForGait(GroundTurnEntryGait);
		const float RecoverySpeed = FMath::Min(
			MaxRecoverySpeed,
			FMath::Max(0.0f, Settings.RunTurnbackRecoveryInitialSpeed) +
				FMath::Max(0.0f, Settings.RunTurnbackRecoveryAcceleration) * RecoveryElapsed);
		const FVector RecoveryVelocity = GroundTurnDesiredDirection * RecoverySpeed;
		CharacterMovement->Velocity.X = RecoveryVelocity.X;
		CharacterMovement->Velocity.Y = RecoveryVelocity.Y;
	}
}

void URoverLocomotionComponent::UpdateMoveStopWatchdog(const float DeltaTime)
{
	if (!bMoveStopPending && !bMoveStopActive)
	{
		return;
	}

	MoveStopStateElapsed += DeltaTime;
	const FRoverMovementSettings& Settings = GetSettings();
	if (bMoveStopPending && MoveStopStateElapsed >= FMath::Max(0.1f, Settings.MoveStopPendingTimeout))
	{
		UE_LOG(LogTemp, Warning, TEXT("Move stop request %d timed out before its animation state entered; completing from the watchdog."), MoveStopRequestId);
		CompleteMoveStop(MoveStopRequestId);
	}
	else if (bMoveStopActive && MoveStopStateElapsed >= FMath::Max(0.1f, Settings.MoveStopActiveTimeout))
	{
		UE_LOG(LogTemp, Warning, TEXT("Move stop request %d timed out before its animation state exited; completing from the watchdog."), MoveStopRequestId);
		CompleteMoveStop(MoveStopRequestId);
	}
}

bool URoverLocomotionComponent::ShouldRequestReleaseStop() const
{
	const bool bSupportedGait = Gait == ERoverGait::Walk ||
		Gait == ERoverGait::Run ||
		Gait == ERoverGait::Sprint;
	if (!CharacterOwner || !CharacterMovement || !CharacterMovement->IsMovingOnGround() ||
		bGroundTurnPending || bGroundTurnActive || bMoveStopPending || bMoveStopActive ||
		InputLockRemaining > 0.0f || LandingType != ERoverLandingType::None ||
		!bSupportedGait)
	{
		return false;
	}

	const FRoverMovementSettings& Settings = GetSettings();
	const float MinimumSpeed = Gait == ERoverGait::Walk
		? Settings.MoveStopWalkMinSpeed
		: (Gait == ERoverGait::Sprint
			? Settings.MoveStopSprintMinSpeed
			: Settings.MoveStopRunMinSpeed);
	return CharacterOwner->GetVelocity().Size2D() >= MinimumSpeed;
}

void URoverLocomotionComponent::RequestReleaseStop()
{
	if (!ShouldRequestReleaseStop())
	{
		return;
	}

	MoveStopGait = Gait;
	MoveStopDesiredDirection = CharacterOwner->GetVelocity().GetSafeNormal2D();
	MoveStopStateElapsed = 0.0f;
	bMoveStopUseLeftVariant = ResolveMoveStopUseLeftVariant();
	bNextMoveStopUsesLeftVariant = !bMoveStopUseLeftVariant;
	bMoveStopPending = true;
	bMoveStopActive = false;
	bMoveStopResumeMovement = false;
	MoveStopRequestId = MoveStopRequestId == MAX_int32 ? 1 : MoveStopRequestId + 1;
}

bool URoverLocomotionComponent::ResolveMoveStopUseLeftVariant() const
{
	if (!CharacterOwner)
	{
		return bNextMoveStopUsesLeftVariant;
	}

	const USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh();
	static const FName LeftFootBone(TEXT("Bip001LFoot"));
	static const FName RightFootBone(TEXT("Bip001RFoot"));
	if (!Mesh || Mesh->GetBoneIndex(LeftFootBone) == INDEX_NONE || Mesh->GetBoneIndex(RightFootBone) == INDEX_NONE)
	{
		return bNextMoveStopUsesLeftVariant;
	}

	FVector TravelDirection = CharacterOwner->GetVelocity().GetSafeNormal2D();
	if (TravelDirection.IsNearlyZero())
	{
		TravelDirection = CharacterOwner->GetActorForwardVector().GetSafeNormal2D();
	}
	const FVector Origin = CharacterOwner->GetActorLocation();
	const float LeftProjection = FVector::DotProduct(Mesh->GetBoneLocation(LeftFootBone) - Origin, TravelDirection);
	const float RightProjection = FVector::DotProduct(Mesh->GetBoneLocation(RightFootBone) - Origin, TravelDirection);
	if (FMath::IsNearlyEqual(LeftProjection, RightProjection, 0.5f))
	{
		return bNextMoveStopUsesLeftVariant;
	}
	// The source _L clips start with the right foot leading; _R is the inverse.
	return LeftProjection < RightProjection;
}

void URoverLocomotionComponent::UpdateGroundTurnRequest()
{
	if (!CharacterOwner || !CharacterMovement || !CharacterMovement->IsMovingOnGround() || MoveWorldDirection.IsNearlyZero())
	{
		return;
	}
	if (InputLockRemaining > 0.0f || LandingType == ERoverLandingType::Heavy || LandingType == ERoverLandingType::Roll)
	{
		if (bGroundTurnPending)
		{
			CancelGroundTurn();
		}
		return;
	}
	if (bGroundTurnActive)
	{
		return;
	}

	const FRoverMovementSettings& Settings = GetSettings();
	const FVector ActorForward = CharacterOwner->GetActorForwardVector().GetSafeNormal2D();
	const FVector ActorRight = CharacterOwner->GetActorRightVector().GetSafeNormal2D();
	const float ForwardAmount = FVector::DotProduct(ActorForward, MoveWorldDirection);
	const float RightAmount = FVector::DotProduct(ActorRight, MoveWorldDirection);
	float SignedAngle = FMath::RadiansToDegrees(FMath::Atan2(RightAmount, ForwardAmount));
	if (FMath::IsNearlyEqual(FMath::Abs(SignedAngle), 180.0f, 0.1f))
	{
		SignedAngle = 180.0f;
	}
	const float AbsoluteAngle = FMath::Abs(SignedAngle);
	const float HorizontalSpeed = CharacterOwner->GetVelocity().Size2D();
	const bool bTurnRight = SignedAngle >= 0.0f;
	const bool bAllowsTurnInPlace = Gait == ERoverGait::Idle &&
		MoveInput.Size() < Settings.AnalogWalkThreshold;
	const bool bRequestsSprintTurnback = Gait == ERoverGait::Sprint &&
		bSprintHeld &&
		MoveInput.Size() >= Settings.SprintInputThreshold &&
		HorizontalSpeed >= Settings.RunTurnbackMinSpeed &&
		AbsoluteAngle >= Settings.RunTurnbackAngle;

	if (bGroundTurnPending)
	{
		const bool bStillQualifies = GroundTurnType == ERoverGroundTurnType::TurnInPlace
			? bAllowsTurnInPlace && HorizontalSpeed <= Settings.TurnInPlaceMaxSpeed && AbsoluteAngle >= Settings.TurnInPlaceAngle
			: GroundTurnEntryGait == ERoverGait::Sprint &&
				bSprintHeld &&
				MoveInput.Size() >= Settings.SprintInputThreshold &&
				HorizontalSpeed >= Settings.RunTurnbackMinSpeed &&
				AbsoluteAngle >= Settings.RunTurnbackAngle;
		const float RearSeamTolerance = FMath::Clamp(Settings.GroundTurnRearmAngle, 0.0f, 180.0f);
		const float MinimumSeamDot = FMath::Cos(FMath::DegreesToRadians(RearSeamTolerance));
		const bool bCrossedRearSeam = bTurnRight != bGroundTurnRight &&
			ForwardAmount < 0.0f &&
			FVector::DotProduct(GroundTurnDesiredDirection, MoveWorldDirection) >= MinimumSeamDot;
		if (!bStillQualifies || (bTurnRight != bGroundTurnRight && !bCrossedRearSeam))
		{
			CancelGroundTurn();
			bGroundTurnArmed = AbsoluteAngle <= Settings.GroundTurnRearmAngle;
		}
		return;
	}

	if (!bGroundTurnArmed)
	{
		bGroundTurnArmed = AbsoluteAngle <= Settings.GroundTurnRearmAngle;
		return;
	}

	ERoverGroundTurnType RequestedTurn = ERoverGroundTurnType::None;
	if (bRequestsSprintTurnback)
	{
		RequestedTurn = ERoverGroundTurnType::RunTurnback;
	}
	else if (bAllowsTurnInPlace && HorizontalSpeed <= Settings.TurnInPlaceMaxSpeed && AbsoluteAngle >= Settings.TurnInPlaceAngle)
	{
		RequestedTurn = ERoverGroundTurnType::TurnInPlace;
	}
	if (RequestedTurn == ERoverGroundTurnType::None)
	{
		return;
	}

	GroundTurnType = RequestedTurn;
	GroundTurnDesiredDirection = MoveWorldDirection;
	GroundTurnEntrySpeed = HorizontalSpeed;
	GroundTurnEntryGait = Gait;
	GroundTurnStateElapsed = 0.0f;
	bGroundTurnRight = bTurnRight;
	bGroundTurnPending = true;
	bGroundTurnActive = false;
	bGroundTurnArmed = false;
	bGroundTurnResumeMovement = true;
	GroundTurnRequestId = GroundTurnRequestId == MAX_int32 ? 1 : GroundTurnRequestId + 1;

	if (GroundTurnType == ERoverGroundTurnType::TurnInPlace)
	{
		CharacterMovement->StopMovementImmediately();
		GroundTurnTargetYaw = FRotator::NormalizeAxis(
			CharacterOwner->GetActorRotation().Yaw + (bGroundTurnRight ? 90.0f : -90.0f));
	}
	else
	{
		GroundTurnTargetYaw = GroundTurnDesiredDirection.Rotation().Yaw;
	}
}

void URoverLocomotionComponent::AcknowledgeGroundTurn(const int32 RequestId)
{
	if (!bGroundTurnPending || RequestId != GroundTurnRequestId || GroundTurnType == ERoverGroundTurnType::None)
	{
		return;
	}

	bGroundTurnPending = false;
	bGroundTurnActive = true;
	GroundTurnStateElapsed = 0.0f;
	if (GroundTurnType == ERoverGroundTurnType::RunTurnback && CharacterOwner && CharacterMovement)
	{
		GroundTurnInertiaDirection = CharacterMovement->Velocity.GetSafeNormal2D();
		if (GroundTurnInertiaDirection.IsNearlyZero())
		{
			GroundTurnInertiaDirection = CharacterOwner->GetActorForwardVector().GetSafeNormal2D();
		}
		GroundTurnInertiaDistanceApplied = 0.0f;
		GroundTurnResumeWindowOpenedAt = 0.0f;
		bGroundTurnResumeWindowOpen = false;
		bGroundTurnInertiaBlocked = false;
		bGroundTurnSavedOrientRotationToMovement = CharacterMovement->bOrientRotationToMovement;
		bGroundTurnSavedUseControllerDesiredRotation = CharacterMovement->bUseControllerDesiredRotation;
		bGroundTurnControlsOrientation = true;
		CharacterMovement->bOrientRotationToMovement = false;
		CharacterMovement->bUseControllerDesiredRotation = false;
		CharacterMovement->StopMovementImmediately();

		// Run_Turnback is an immediate direction reversal. The animation supplies
		// the visual turn and the short inertial slide, while the capsule faces
		// the new travel direction from the first active frame.
		FRotator ActorRotation = CharacterOwner->GetActorRotation();
		ActorRotation.Yaw = GroundTurnTargetYaw;
		CharacterOwner->SetActorRotation(ActorRotation);
	}
}

void URoverLocomotionComponent::AcknowledgeRunTurnbackResumeWindow(const int32 RequestId)
{
	if (!bGroundTurnActive || RequestId != GroundTurnRequestId ||
		GroundTurnType != ERoverGroundTurnType::RunTurnback ||
		!bGroundTurnResumeMovement || bGroundTurnResumeWindowOpen)
	{
		return;
	}

	bGroundTurnResumeWindowOpen = true;
	GroundTurnResumeWindowOpenedAt = GroundTurnStateElapsed;
}

void URoverLocomotionComponent::CompleteGroundTurn(const int32 RequestId)
{
	if (!bGroundTurnActive || RequestId != GroundTurnRequestId)
	{
		return;
	}

	if (CharacterOwner && CharacterMovement && CharacterMovement->IsMovingOnGround())
	{
		FRotator ActorRotation = CharacterOwner->GetActorRotation();
		ActorRotation.Yaw = GroundTurnTargetYaw;
		CharacterOwner->SetActorRotation(ActorRotation);

		if (GroundTurnType == ERoverGroundTurnType::RunTurnback &&
			bGroundTurnResumeMovement &&
			!MoveWorldDirection.IsNearlyZero())
		{
			ERoverGait ResumeGait = ERoverGait::Run;
			const FRoverMovementSettings& Settings = GetSettings();
			if (bSprintHeld && MoveInput.Size() >= Settings.SprintInputThreshold)
			{
				ResumeGait = ERoverGait::Sprint;
			}
			else if (Settings.bUseAnalogWalk && MoveInput.Size() < Settings.AnalogWalkThreshold)
			{
				ResumeGait = ERoverGait::Walk;
			}
			LastMovingGait = ResumeGait;
			SetGait(ResumeGait);
			CharacterMovement->MaxWalkSpeed = GetMaxSpeedForGait(ResumeGait);
			const float MaxResumeSpeed = GetMaxSpeedForGait(ResumeGait);
			const float CurrentResumeSpeed = FMath::Max(
				0.0f,
				FVector::DotProduct(CharacterMovement->Velocity, MoveWorldDirection));
			const float MinimumResumeSpeed = FMath::Min(
				FMath::Max(0.0f, Settings.RunTurnbackRecoveryInitialSpeed),
				MaxResumeSpeed);
			const float ResumedSpeed = FMath::Clamp(
				FMath::Max(CurrentResumeSpeed, MinimumResumeSpeed),
				0.0f,
				MaxResumeSpeed);
			const FVector ResumedVelocity = MoveWorldDirection * ResumedSpeed;
			CharacterMovement->Velocity = FVector(ResumedVelocity.X, ResumedVelocity.Y, CharacterMovement->Velocity.Z);
		}
	}

	CancelGroundTurn();
}

void URoverLocomotionComponent::AcknowledgeMoveStop(const int32 RequestId)
{
	if (!bMoveStopPending || RequestId != MoveStopRequestId)
	{
		return;
	}

	bMoveStopPending = false;
	bMoveStopActive = true;
	MoveStopStateElapsed = 0.0f;
}

void URoverLocomotionComponent::CompleteMoveStop(const int32 RequestId)
{
	if (RequestId != MoveStopRequestId || (!bMoveStopPending && !bMoveStopActive))
	{
		return;
	}

	const bool bShouldResume = bMoveStopResumeMovement &&
		!MoveInput.IsNearlyZero() && !MoveWorldDirection.IsNearlyZero();
	CancelMoveStop();

	if (!CharacterOwner || !CharacterMovement || !CharacterMovement->IsMovingOnGround())
	{
		return;
	}

	if (bShouldResume)
	{
		bGroundTurnArmed = false;
		const FRoverMovementSettings& Settings = GetSettings();
		ERoverGait ResumeGait = ERoverGait::Run;
		if (bSprintHeld && MoveInput.Size() >= Settings.SprintInputThreshold)
		{
			ResumeGait = ERoverGait::Sprint;
		}
		else if (Settings.bUseAnalogWalk && MoveInput.Size() < Settings.AnalogWalkThreshold)
		{
			ResumeGait = ERoverGait::Walk;
		}
		LastMovingGait = ResumeGait;
		SetGait(ResumeGait);
		CharacterMovement->MaxWalkSpeed = GetMaxSpeedForGait(ResumeGait);
	}
	else
	{
		CharacterMovement->StopMovementImmediately();
	}
}

bool URoverLocomotionComponent::IsGroundTurnInputCompatible() const
{
	if (MoveWorldDirection.IsNearlyZero() || GroundTurnDesiredDirection.IsNearlyZero())
	{
		return false;
	}

	const float Tolerance = FMath::Clamp(GetSettings().GroundTurnRearmAngle, 0.0f, 180.0f);
	const float MinimumDot = FMath::Cos(FMath::DegreesToRadians(Tolerance));
	return FVector::DotProduct(MoveWorldDirection, GroundTurnDesiredDirection) >= MinimumDot;
}

float URoverLocomotionComponent::GetMaxSpeedForGait(const ERoverGait InGait) const
{
	const FRoverMovementSettings& Settings = GetSettings();
	switch (InGait)
	{
	case ERoverGait::Walk:
		return Settings.WalkSpeed;
	case ERoverGait::Sprint:
		return Settings.SprintSpeed;
	case ERoverGait::Idle:
	case ERoverGait::Run:
	default:
		return Settings.RunSpeed;
	}
}

void URoverLocomotionComponent::CancelGroundTurn()
{
	if (bGroundTurnControlsOrientation && CharacterMovement)
	{
		CharacterMovement->bOrientRotationToMovement = bGroundTurnSavedOrientRotationToMovement;
		CharacterMovement->bUseControllerDesiredRotation = bGroundTurnSavedUseControllerDesiredRotation;
	}
	bGroundTurnControlsOrientation = false;
	bGroundTurnPending = false;
	bGroundTurnActive = false;
	GroundTurnType = ERoverGroundTurnType::None;
	GroundTurnDesiredDirection = FVector::ZeroVector;
	GroundTurnInertiaDirection = FVector::ZeroVector;
	GroundTurnTargetYaw = 0.0f;
	GroundTurnEntrySpeed = 0.0f;
	GroundTurnInertiaDistanceApplied = 0.0f;
	GroundTurnResumeWindowOpenedAt = 0.0f;
	GroundTurnStateElapsed = 0.0f;
	GroundTurnEntryGait = ERoverGait::Idle;
	bGroundTurnResumeMovement = false;
	bGroundTurnResumeWindowOpen = false;
	bGroundTurnInertiaBlocked = false;
}

void URoverLocomotionComponent::CancelMoveStop()
{
	bMoveStopPending = false;
	bMoveStopActive = false;
	MoveStopStateElapsed = 0.0f;
	MoveStopGait = ERoverGait::Idle;
	MoveStopDesiredDirection = FVector::ZeroVector;
	bMoveStopResumeMovement = false;
}

void URoverLocomotionComponent::SetLocomotionState(const ERoverLocomotionState NewState)
{
	if (LocomotionState == NewState)
	{
		return;
	}

	const ERoverLocomotionState PreviousState = LocomotionState;
	LocomotionState = NewState;
	OnLocomotionStateChanged.Broadcast(PreviousState, LocomotionState);
}

void URoverLocomotionComponent::SetGait(const ERoverGait NewGait)
{
	if (Gait == NewGait)
	{
		return;
	}

	const ERoverGait PreviousGait = Gait;
	Gait = NewGait;
	OnGaitChanged.Broadcast(PreviousGait, Gait);
	if (Gait == ERoverGait::Sprint)
	{
		OnSprintStarted.Broadcast();
	}
}
