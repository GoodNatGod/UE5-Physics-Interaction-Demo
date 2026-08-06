#include "RoverAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "RoverCharacter.h"
#include "RoverCombatComponent.h"
#include "RoverLocomotionComponent.h"

namespace
{
float ResolveMoveStopDuration(const ERoverGait Gait, const bool bUseLeftVariant)
{
	const TCHAR* GaitName = Gait == ERoverGait::Walk
		? TEXT("Walk")
		: (Gait == ERoverGait::Sprint ? TEXT("Sprint") : TEXT("Run"));
	const TCHAR* VariantName = bUseLeftVariant ? TEXT("L") : TEXT("R");
	const FString SequenceName = FString::Printf(TEXT("Stop_%s_%s"), GaitName, VariantName);
	const FString ObjectPath = FString::Printf(
		TEXT("/Game/Rover/Animations/P0/%s.%s"),
		*SequenceName,
		*SequenceName);
	if (const UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *ObjectPath))
	{
		return Sequence->GetPlayLength();
	}
	return 0.75f;
}
}

void URoverAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	SetRootMotionMode(ERootMotionMode::RootMotionFromEverything);
	RoverCharacter = Cast<ARoverCharacter>(TryGetPawnOwner());
	LocomotionComponent = RoverCharacter ? RoverCharacter->GetLocomotionComponent() : nullptr;
	CombatComponent = RoverCharacter ? RoverCharacter->GetCombatComponent() : nullptr;
	PreviousActorYaw = RoverCharacter ? RoverCharacter->GetActorRotation().Yaw : 0.0f;
	PreviousGait = LocomotionComponent ? LocomotionComponent->GetGait() : ERoverGait::Idle;
	bPreviousSecondJumpUsed = LocomotionComponent && LocomotionComponent->HasUsedSecondJump();
	ObservedGroundJumpRequestId = LocomotionComponent ? LocomotionComponent->GetGroundJumpRequestId() : 0;
	ObservedGroundTurnRequestId = LocomotionComponent ? LocomotionComponent->GetGroundTurnRequestId() : 0;
	bGroundTurnRight = !LocomotionComponent || LocomotionComponent->DoesGroundTurnRight();
	ObservedMoveStopRequestId = LocomotionComponent ? LocomotionComponent->GetMoveStopRequestId() : 0;
	ObservedAttackRequestId = CombatComponent ? CombatComponent->GetAttackRequestId() : 0;
	ObservedHitReactionRequestId = CombatComponent ? CombatComponent->GetHitReactionRequestId() : 0;
	bMoveStopUseLeftVariant = !LocomotionComponent || LocomotionComponent->DoesMoveStopUseLeftVariant();
	ResetIdleVariationTimer();
}

void URoverAnimInstance::NativeUpdateAnimation(const float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!RoverCharacter)
	{
		RoverCharacter = Cast<ARoverCharacter>(TryGetPawnOwner());
		LocomotionComponent = RoverCharacter ? RoverCharacter->GetLocomotionComponent() : nullptr;
		CombatComponent = RoverCharacter ? RoverCharacter->GetCombatComponent() : nullptr;
	}
	if (!RoverCharacter || !LocomotionComponent || !CombatComponent)
	{
		return;
	}

	const FVector Velocity = RoverCharacter->GetVelocity();
	const FVector HorizontalVelocity(Velocity.X, Velocity.Y, 0.0f);
	GroundSpeed = HorizontalVelocity.Size();
	VerticalVelocity = Velocity.Z;
	InputMagnitude = LocomotionComponent->GetInputMagnitude();
	LocomotionState = LocomotionComponent->GetLocomotionState();
	Gait = LocomotionComponent->GetGait();
	LandingType = LocomotionComponent->GetLandingType();
	bIsFalling = LocomotionState == ERoverLocomotionState::Airborne;
	bHasUsedSecondJump = LocomotionComponent->HasUsedSecondJump();
	const FRoverMovementSettings& Settings = LocomotionComponent->GetSettings();
	const bool bHasAcceptedMoveIntent = LocomotionState == ERoverLocomotionState::Grounded &&
		InputMagnitude > FMath::Clamp(Settings.InputDeadZone, 0.0f, 1.0f) &&
		LocomotionComponent->CanAcceptMovementInput();
	bShouldMove = GroundSpeed > 3.0f || bHasAcceptedMoveIntent;
	bHasAcceleration = RoverCharacter->GetCharacterMovement()->GetCurrentAcceleration().SizeSquared2D() > 1.0f;
	bIsWalking = bShouldMove && Gait == ERoverGait::Walk;
	bIsRunning = bShouldMove && Gait == ERoverGait::Run;
	bIsSprinting = bShouldMove && Gait == ERoverGait::Sprint;
	bIsDescending = bIsFalling && VerticalVelocity <= 0.0f;
	const float DownwardSpeed = FMath::Max(0.0f, -VerticalVelocity);
	const float FastFallFullSpeed = FMath::Max(Settings.FastFallFullSpeed, Settings.FastFallBlendStartSpeed + 1.0f);
	FastFallAlpha = bIsDescending
		? FMath::GetMappedRangeValueClamped(
			FVector2D(Settings.FastFallBlendStartSpeed, FastFallFullSpeed),
			FVector2D(0.0f, 1.0f),
			DownwardSpeed)
		: 0.0f;
	bSprintImpulseRequested = PreviousGait != ERoverGait::Sprint && Gait == ERoverGait::Sprint;
	const int32 GroundJumpRequestId = LocomotionComponent->GetGroundJumpRequestId();
	if (!bIsFalling)
	{
		ObservedGroundJumpRequestId = GroundJumpRequestId;
	}
	bJumpStartRequested = bIsFalling && GroundJumpRequestId != ObservedGroundJumpRequestId;
	bJumpStartedFromRun = LocomotionComponent->DidGroundJumpStartFromRun();
	bJumpOffLeftFoot = LocomotionComponent->DidGroundJumpUseLeftFoot();
	bSecondJumpRequested = !bPreviousSecondJumpUsed && bHasUsedSecondJump;
	bSecondJumpBackward = LocomotionComponent->DidSecondJumpMoveBackward();
	bLandingLight = !bIsFalling && LandingType == ERoverLandingType::Light;
	bLandingHeavy = !bIsFalling && LandingType == ERoverLandingType::Heavy;
	bLandingRoll = !bIsFalling && LandingType == ERoverLandingType::Roll;
	bIsLanding = bLandingLight || bLandingHeavy || bLandingRoll;
	bGroundedWithoutLanding = !bIsFalling && !bIsLanding;
	const int32 GroundTurnRequestId = LocomotionComponent->GetGroundTurnRequestId();
	if (GroundTurnRequestId != ObservedGroundTurnRequestId)
	{
		ObservedGroundTurnRequestId = GroundTurnRequestId;
		bGroundTurnRight = LocomotionComponent->DoesGroundTurnRight();
	}
	const ERoverGroundTurnType GroundTurnType = LocomotionComponent->GetGroundTurnType();
	bTurnInPlaceRequested = LocomotionComponent->IsGroundTurnPending() && GroundTurnType == ERoverGroundTurnType::TurnInPlace;
	bRunTurnbackRequested = LocomotionComponent->IsGroundTurnPending() && GroundTurnType == ERoverGroundTurnType::RunTurnback;
	const int32 MoveStopRequestId = LocomotionComponent->GetMoveStopRequestId();
	if (MoveStopRequestId != ObservedMoveStopRequestId)
	{
		ObservedMoveStopRequestId = MoveStopRequestId;
		bMoveStopUseLeftVariant = LocomotionComponent->DoesMoveStopUseLeftVariant();
		const ERoverGait MoveStopGait = LocomotionComponent->GetMoveStopGait();
		bMoveStopWasRunning = MoveStopGait == ERoverGait::Run;
		bMoveStopWasSprinting = MoveStopGait == ERoverGait::Sprint;
		MoveStopRemainingTime = ResolveMoveStopDuration(MoveStopGait, bMoveStopUseLeftVariant);
		MoveStopElapsedTime = 0.0f;
	}
	bMoveStopRequested = LocomotionComponent->IsMoveStopPending();
	const bool bMoveStopActive = LocomotionComponent->IsMoveStopActive();
	if (bMoveStopActive)
	{
		MoveStopRemainingTime = FMath::Max(0.0f, MoveStopRemainingTime - DeltaSeconds);
		MoveStopElapsedTime += DeltaSeconds;
	}
	else if (!bMoveStopRequested)
	{
		MoveStopRemainingTime = 0.0f;
		MoveStopElapsedTime = 0.0f;
	}
	const float MoveStopDuration = MoveStopRemainingTime + MoveStopElapsedTime;
	const bool bMoveStopWasWalking = !bMoveStopWasRunning && !bMoveStopWasSprinting;
	const float ConfiguredResumeNormalizedTime = bMoveStopWasWalking
		? Settings.MoveStopWalkResumeNormalizedTime
		: Settings.MoveStopResumeNormalizedTime;
	const float ResumeNormalizedTime = FMath::Clamp(ConfiguredResumeNormalizedTime, 0.1f, 1.0f);
	const bool bResumeWindowReached = LocomotionComponent->ShouldMoveStopResumeMovement() &&
		MoveStopElapsedTime >= MoveStopDuration * ResumeNormalizedTime;
	bMoveStopShouldExit = !bMoveStopRequested &&
		(!bMoveStopActive || bResumeWindowReached || MoveStopRemainingTime <= 0.12f);

	CombatPhase = CombatComponent->GetCombatPhase();
	HitReactionType = CombatComponent->GetHitReactionType();
	const bool bWasAttacking = bIsAttacking;
	bIsAttacking = CombatComponent->IsAttacking();
	bIsInHitReaction = CombatComponent->IsInHitReaction();

	// Preserve the final attack pose by matching the idle stance to the forward foot.
	const bool bAttackJustEnded = bWasAttacking && !bIsAttacking;
	const int32 EndedAttackRequestId = ObservedAttackRequestId > 0
		? ObservedAttackRequestId
		: LastEndedAttackRequestId;
	if (bAttackJustEnded && EndedAttackRequestId > 0 &&
		!ActiveAttackMontage &&
		PostAttackStanceCapturedRequestId != EndedAttackRequestId &&
		CapturePostAttackIdleStance())
	{
		PostAttackStanceCapturedRequestId = EndedAttackRequestId;
	}
	const int32 HitReactionRequestId = CombatComponent->GetHitReactionRequestId();
	if (HitReactionRequestId > 0 && HitReactionRequestId != ObservedHitReactionRequestId)
	{
		ObservedHitReactionRequestId = HitReactionRequestId;
		PlayPendingHitReaction(HitReactionRequestId);
	}
	const int32 AttackRequestId = CombatComponent->GetAttackRequestId();
	if (AttackRequestId > 0 && AttackRequestId != ObservedAttackRequestId && !bIsInHitReaction)
	{
		ObservedAttackRequestId = AttackRequestId;
		PlayPendingAttack(AttackRequestId);
	}
	TryCapturePostAttackIdleStanceBeforeBlendOut(DeltaSeconds);

	const bool bIdleVariationEligible = bGroundedWithoutLanding &&
		!bShouldMove &&
		!bIsAttacking &&
		!bIsInHitReaction &&
		InputMagnitude <= FMath::Clamp(Settings.InputDeadZone, 0.0f, 1.0f) &&
		!LocomotionComponent->IsGroundTurnPending() &&
		!LocomotionComponent->IsGroundTurnActive() &&
		!LocomotionComponent->IsMoveStopPending() &&
		!LocomotionComponent->IsMoveStopActive();
	if (bIdleVariationEligible)
	{
		if (!bWasIdleVariationEligible)
		{
			ResetIdleVariationTimer();
		}
		IdleVariationRemaining -= DeltaSeconds;
		if (IdleVariationRemaining <= 0.0f)
		{
			if (!bFootStanceLocked)
			{
				bUseStand2 = !bUseStand2;
			}
			ResetIdleVariationTimer();
		}
	}
	else
	{
		if (bWasIdleVariationEligible || bUseStand2)
		{
			ResetIdleVariationTimer();
		}
		// Locomotion starts from the canonical Stand1 pose.
		const bool bLocomotionStarted = bHasAcceptedMoveIntent || !bGroundedWithoutLanding;
		if (bLocomotionStarted)
		{
			bFootStanceLocked = false;
			bUseStand2 = false;
		}
	}
	bWasIdleVariationEligible = bIdleVariationEligible;
	const float IdleStanceTarget = bUseStand2 ? 1.0f : 0.0f;
	const float IdleStanceBlendTime = FMath::Max(0.0f, Settings.IdleStanceBlendTime);
	IdleStanceAlpha = bFootStanceLocked || IdleStanceBlendTime <= UE_SMALL_NUMBER
		? IdleStanceTarget
		: FMath::FInterpConstantTo(
			IdleStanceAlpha,
			IdleStanceTarget,
			FMath::Max(0.0f, DeltaSeconds),
			1.0f / IdleStanceBlendTime);

	if (GroundSpeed > 1.0f)
	{
		const FVector VelocityDirection = HorizontalVelocity.GetSafeNormal();
		const float ForwardAmount = FVector::DotProduct(RoverCharacter->GetActorForwardVector(), VelocityDirection);
		const float RightAmount = FVector::DotProduct(RoverCharacter->GetActorRightVector(), VelocityDirection);
		Direction = FMath::RadiansToDegrees(FMath::Atan2(RightAmount, ForwardAmount));
	}
	else
	{
		Direction = 0.0f;
	}
	bMovingBackward = FMath::Abs(Direction) > 90.0f;

	if (DeltaSeconds > UE_SMALL_NUMBER)
	{
		const float ActorYaw = RoverCharacter->GetActorRotation().Yaw;
		const float YawSpeed = FMath::FindDeltaAngleDegrees(PreviousActorYaw, ActorYaw) / DeltaSeconds;
		const float TargetLean = FMath::Clamp(YawSpeed / 360.0f, -1.0f, 1.0f);
		Lean = FMath::FInterpTo(Lean, TargetLean, DeltaSeconds, 6.0f);
		PreviousActorYaw = ActorYaw;
	}

	PreviousGait = Gait;
	bPreviousSecondJumpUsed = bHasUsedSecondJump;
}

void URoverAnimInstance::AnimNotify_RoverGroundTurnEntered()
{
	if (LocomotionComponent)
	{
		LocomotionComponent->AcknowledgeGroundTurn(ObservedGroundTurnRequestId);
	}
}

void URoverAnimInstance::AnimNotify_RoverGroundTurnExited()
{
	if (LocomotionComponent)
	{
		LocomotionComponent->CompleteGroundTurn(ObservedGroundTurnRequestId);
	}
}

void URoverAnimInstance::AnimNotify_RoverMoveStopEntered()
{
	if (LocomotionComponent)
	{
		LocomotionComponent->AcknowledgeMoveStop(ObservedMoveStopRequestId);
	}
}

void URoverAnimInstance::AnimNotify_RoverMoveStopExited()
{
	if (LocomotionComponent)
	{
		LocomotionComponent->CompleteMoveStop(ObservedMoveStopRequestId);
	}
}

void URoverAnimInstance::PlayPendingAttack(const int32 RequestId)
{
	UAnimMontage* Montage = CombatComponent ? CombatComponent->ResolveAttackMontage() : nullptr;
	if (ActiveAttackMontage && ActiveAttackMontage != Montage && Montage_IsPlaying(ActiveAttackMontage))
	{
		UAnimMontage* PreviousMontage = ActiveAttackMontage;
		ActiveAttackMontage = nullptr;
		Montage_Stop(0.0f, PreviousMontage);
	}
	const float PlayRate = CombatComponent ? CombatComponent->ResolveAttackPlayRate() : 1.0f;
	if (!Montage || Montage_Play(Montage, PlayRate) <= 0.0f)
	{
		if (RoverCharacter)
		{
			RoverCharacter->HandleAttackAnimationRejected(RequestId);
		}
		return;
	}
	ActiveAttackMontage = Montage;

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &URoverAnimInstance::OnAttackMontageEnded, RequestId);
	Montage_SetEndDelegate(EndDelegate, Montage);
}

void URoverAnimInstance::PlayAttackRequestImmediately(const int32 RequestId)
{
	if (RequestId <= 0)
	{
		return;
	}
	ObservedAttackRequestId = RequestId;
	PlayPendingAttack(RequestId);
}

void URoverAnimInstance::StopAttackRequest(const int32 RequestId, const float BlendOutTime)
{
	if (RequestId <= 0 || !ActiveAttackMontage)
	{
		return;
	}

	UAnimMontage* Montage = ActiveAttackMontage;
	ActiveAttackMontage = nullptr;
	ObservedAttackRequestId = 0;
	PostAttackStanceCapturedRequestId = RequestId;
	LastEndedAttackRequestId = RequestId;
	Montage_Stop(FMath::Max(0.0f, BlendOutTime), Montage);
}

void URoverAnimInstance::HandleComboWindowStateBegin()
{
	if (RoverCharacter)
	{
		RoverCharacter->HandleComboWindowBeginNotify(ObservedAttackRequestId);
	}
}

void URoverAnimInstance::HandleComboWindowStateEnd()
{
	if (RoverCharacter)
	{
		RoverCharacter->HandleComboWindowEndNotify(ObservedAttackRequestId);
	}
}

void URoverAnimInstance::PlayPendingHitReaction(const int32 RequestId)
{
	UAnimMontage* Montage = CombatComponent ? CombatComponent->ResolveHitReactionMontage() : nullptr;
	if (!Montage || Montage_Play(Montage, 1.0f) <= 0.0f)
	{
		if (RoverCharacter)
		{
			RoverCharacter->HandleHitReactionAnimationRejected(RequestId);
		}
		return;
	}

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &URoverAnimInstance::OnHitReactionMontageEnded, RequestId);
	Montage_SetEndDelegate(EndDelegate, Montage);
}

void URoverAnimInstance::OnAttackMontageEnded(UAnimMontage* Montage, const bool bInterrupted, const int32 RequestId)
{
	if (!bInterrupted && PostAttackStanceCapturedRequestId != RequestId && CapturePostAttackIdleStance())
	{
		PostAttackStanceCapturedRequestId = RequestId;
	}
	LastEndedAttackRequestId = RequestId;

	if (ActiveAttackMontage == Montage)
	{
		ActiveAttackMontage = nullptr;
	}
	if (RoverCharacter)
	{
		RoverCharacter->HandleAttackMontageEnded(RequestId, bInterrupted);
	}
}

void URoverAnimInstance::OnHitReactionMontageEnded(UAnimMontage* Montage, const bool bInterrupted, const int32 RequestId)
{
	if (RoverCharacter)
	{
		RoverCharacter->HandleHitReactionMontageEnded(RequestId, bInterrupted);
	}
}

void URoverAnimInstance::AnimNotify_RoverAttackStarted()
{
	if (RoverCharacter) RoverCharacter->HandleAttackStartedNotify(ObservedAttackRequestId);
}

void URoverAnimInstance::AnimNotify_RoverAttackActiveBegin()
{
	if (RoverCharacter) RoverCharacter->HandleAttackActiveBeginNotify(ObservedAttackRequestId);
}

void URoverAnimInstance::AnimNotify_RoverAttackActiveEnd()
{
	if (RoverCharacter) RoverCharacter->HandleAttackActiveEndNotify(ObservedAttackRequestId);
}

void URoverAnimInstance::AnimNotify_RoverComboWindowBegin()
{
	HandleComboWindowStateBegin();
}

void URoverAnimInstance::AnimNotify_RoverComboWindowEnd()
{
	HandleComboWindowStateEnd();
}

void URoverAnimInstance::AnimNotify_RoverRecoveryBegin()
{
	if (RoverCharacter) RoverCharacter->HandleAttackRecoveryBeginNotify(ObservedAttackRequestId);
}

void URoverAnimInstance::AnimNotify_RoverAttackFinished()
{
	if (RoverCharacter) RoverCharacter->HandleAttackFinishedNotify(ObservedAttackRequestId);
}

void URoverAnimInstance::AnimNotify_RoverHitReactionStarted()
{
	if (RoverCharacter) RoverCharacter->HandleHitReactionStartedNotify(ObservedHitReactionRequestId);
}

void URoverAnimInstance::AnimNotify_RoverHitReactionFinished()
{
	if (RoverCharacter) RoverCharacter->HandleHitReactionFinishedNotify(ObservedHitReactionRequestId);
}

void URoverAnimInstance::UpdateIdleStanceFromFootPosition()
{
	if (!RoverCharacter)
	{
		return;
	}

	const USkeletalMeshComponent* Mesh = RoverCharacter->GetMesh();
	static const FName LeftFootBone(TEXT("Bip001LFoot"));
	static const FName RightFootBone(TEXT("Bip001RFoot"));
	if (!Mesh || Mesh->GetBoneIndex(LeftFootBone) == INDEX_NONE || Mesh->GetBoneIndex(RightFootBone) == INDEX_NONE)
	{
		return;
	}

	const FVector ActorForward = RoverCharacter->GetActorForwardVector();
	const FVector Origin = RoverCharacter->GetActorLocation();
	const float LeftProjection = FVector::DotProduct(
		Mesh->GetBoneLocation(LeftFootBone, EBoneSpaces::WorldSpace) - Origin,
		ActorForward);
	const float RightProjection = FVector::DotProduct(
		Mesh->GetBoneLocation(RightFootBone, EBoneSpaces::WorldSpace) - Origin,
		ActorForward);

	// If the feet are too close to distinguish, lock the current stance.
	const FRoverMovementSettings& Settings = LocomotionComponent->GetSettings();
	const float ProjectionTolerance = FMath::Max(0.0f, Settings.IdleFootStanceProjectionTolerance);
	if (!FMath::IsNearlyEqual(LeftProjection, RightProjection, ProjectionTolerance))
	{
		bUseStand2 = RightProjection > LeftProjection;
	}

	bFootStanceLocked = true;
	IdleStanceAlpha = bUseStand2 ? 1.0f : 0.0f;
}

bool URoverAnimInstance::CapturePostAttackIdleStance()
{
	if (!LocomotionComponent || !bGroundedWithoutLanding)
	{
		return false;
	}

	const FRoverMovementSettings& Settings = LocomotionComponent->GetSettings();
	const bool bHasMoveIntent =
		LocomotionComponent->GetInputMagnitude() > FMath::Clamp(Settings.InputDeadZone, 0.0f, 1.0f);
	if (!bHasMoveIntent)
	{
		UpdateIdleStanceFromFootPosition();
		return true;
	}
	return false;
}

void URoverAnimInstance::TryCapturePostAttackIdleStanceBeforeBlendOut(const float DeltaSeconds)
{
	if (!ActiveAttackMontage || ObservedAttackRequestId <= 0 ||
		PostAttackStanceCapturedRequestId == ObservedAttackRequestId ||
		!Montage_IsPlaying(ActiveAttackMontage))
	{
		return;
	}

	const float BlendOutLeadTime = ActiveAttackMontage->BlendOutTriggerTime >= 0.0f
		? ActiveAttackMontage->BlendOutTriggerTime
		: ActiveAttackMontage->GetDefaultBlendOutTime();
	const float CapturePosition = FMath::Max(
		0.0f,
		ActiveAttackMontage->GetPlayLength() - FMath::Max(0.0f, BlendOutLeadTime));
	const float CurrentPosition = Montage_GetPosition(ActiveAttackMontage);
	const float PlayRate = FMath::Max(0.0f, Montage_GetPlayRate(ActiveAttackMontage));
	const float PredictedPosition = CurrentPosition + FMath::Max(0.0f, DeltaSeconds) * PlayRate;

	// Bone transforms still contain the previous evaluated pose here. Capture on the
	// crossing frame so the idle selector sees the last pose before automatic blend-out.
	if ((CurrentPosition >= CapturePosition || PredictedPosition >= CapturePosition) &&
		CapturePostAttackIdleStance())
	{
		PostAttackStanceCapturedRequestId = ObservedAttackRequestId;
	}
}

float URoverAnimInstance::GetIdleFootStanceProjectionTolerance() const
{
	return LocomotionComponent
		? FMath::Max(0.0f, LocomotionComponent->GetSettings().IdleFootStanceProjectionTolerance)
		: 0.0f;
}

void URoverAnimInstance::ResetIdleVariationTimer()
{
	if (!LocomotionComponent)
	{
		IdleVariationRemaining = 15.0f;
		return;
	}

	const FRoverMovementSettings& Settings = LocomotionComponent->GetSettings();
	const float MinimumTime = FMath::Max(0.0f, Settings.IdleVariationMinTime);
	const float MaximumTime = FMath::Max(MinimumTime, Settings.IdleVariationMaxTime);
	IdleVariationRemaining = FMath::FRandRange(MinimumTime, MaximumTime);
}
