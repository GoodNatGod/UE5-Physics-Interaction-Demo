#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "RoverCombatTypes.h"
#include "RoverLocomotionTypes.h"
#include "RoverAnimInstance.generated.h"

class ARoverCharacter;
class UAnimMontage;
class URoverCombatComponent;
class URoverLocomotionComponent;

UCLASS(Blueprintable, BlueprintType)
class ROVERREPLICA_API URoverAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	void PlayAttackRequestImmediately(int32 RequestId);
	void StopAttackRequest(int32 RequestId, float BlendOutTime);
	void HandleComboWindowStateBegin();
	void HandleComboWindowStateEnd();

	UFUNCTION(BlueprintPure, Category = "Rover|Locomotion")
	bool IsUsingStand2() const { return bUseStand2; }

	UFUNCTION(BlueprintPure, Category = "Rover|Locomotion")
	bool IsFootStanceLocked() const { return bFootStanceLocked; }

	UFUNCTION(BlueprintPure, Category = "Rover|Locomotion")
	float GetIdleStanceAlpha() const { return IdleStanceAlpha; }

	UFUNCTION(BlueprintPure, Category = "Rover|Locomotion")
	float GetIdleFootStanceProjectionTolerance() const;

	UFUNCTION()
	void AnimNotify_RoverGroundTurnEntered();

	UFUNCTION()
	void AnimNotify_RoverGroundTurnExited();

	UFUNCTION()
	void AnimNotify_RoverMoveStopEntered();

	UFUNCTION()
	void AnimNotify_RoverMoveStopExited();

	UFUNCTION()
	void AnimNotify_RoverAttackStarted();

	UFUNCTION()
	void AnimNotify_RoverAttackActiveBegin();

	UFUNCTION()
	void AnimNotify_RoverAttackActiveEnd();

	UFUNCTION()
	void AnimNotify_RoverComboWindowBegin();

	UFUNCTION()
	void AnimNotify_RoverComboWindowEnd();

	UFUNCTION()
	void AnimNotify_RoverRecoveryBegin();

	UFUNCTION()
	void AnimNotify_RoverAttackFinished();

	UFUNCTION()
	void AnimNotify_RoverHitReactionStarted();

	UFUNCTION()
	void AnimNotify_RoverHitReactionFinished();

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	float GroundSpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	float VerticalVelocity = 0.0f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	float Direction = 0.0f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	float InputMagnitude = 0.0f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	float Lean = 0.0f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	float FastFallAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bShouldMove = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bHasAcceleration = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bIsFalling = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bHasUsedSecondJump = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bIsWalking = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bIsRunning = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bIsSprinting = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bIsDescending = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bMovingBackward = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bSprintImpulseRequested = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bJumpStartRequested = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bJumpStartedFromRun = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bJumpOffLeftFoot = true;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bSecondJumpRequested = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bSecondJumpBackward = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bLandingLight = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bLandingHeavy = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bLandingRoll = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bIsLanding = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bGroundedWithoutLanding = true;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bUseStand2 = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	float IdleStanceAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bTurnInPlaceRequested = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bRunTurnbackRequested = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bGroundTurnRight = true;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bMoveStopRequested = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bMoveStopShouldExit = true;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bMoveStopUseLeftVariant = true;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bMoveStopWasRunning = true;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	bool bMoveStopWasSprinting = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	ERoverLocomotionState LocomotionState = ERoverLocomotionState::Grounded;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	ERoverGait Gait = ERoverGait::Idle;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Locomotion")
	ERoverLandingType LandingType = ERoverLandingType::None;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Combat")
	ERoverCombatPhase CombatPhase = ERoverCombatPhase::None;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Combat")
	ERoverHitReactionType HitReactionType = ERoverHitReactionType::None;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Combat")
	bool bIsAttacking = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Rover|Combat")
	bool bIsInHitReaction = false;

private:
	void ResetIdleVariationTimer();
	void PlayPendingAttack(int32 RequestId);
	void PlayPendingHitReaction(int32 RequestId);
	void OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 RequestId);
	void OnHitReactionMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 RequestId);
	void UpdateIdleStanceFromFootPosition();
	bool CapturePostAttackIdleStance();
	void TryCapturePostAttackIdleStanceBeforeBlendOut(float DeltaSeconds);

	UPROPERTY(Transient)
	TObjectPtr<ARoverCharacter> RoverCharacter;

	UPROPERTY(Transient)
	TObjectPtr<URoverLocomotionComponent> LocomotionComponent;

	UPROPERTY(Transient)
	TObjectPtr<URoverCombatComponent> CombatComponent;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveAttackMontage;

	float PreviousActorYaw = 0.0f;
	float IdleVariationRemaining = 0.0f;
	int32 ObservedGroundJumpRequestId = 0;
	int32 ObservedGroundTurnRequestId = 0;
	int32 ObservedMoveStopRequestId = 0;
	int32 ObservedAttackRequestId = 0;
	int32 ObservedHitReactionRequestId = 0;
	float MoveStopRemainingTime = 0.0f;
	float MoveStopElapsedTime = 0.0f;
	ERoverGait PreviousGait = ERoverGait::Idle;
	bool bPreviousSecondJumpUsed = false;
	bool bWasIdleVariationEligible = false;
	bool bFootStanceLocked = false;
	int32 PostAttackStanceCapturedRequestId = 0;
	int32 LastEndedAttackRequestId = 0;
};
