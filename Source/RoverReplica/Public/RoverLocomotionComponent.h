#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RoverLocomotionTypes.h"
#include "RoverMovementConfig.h"
#include "RoverLocomotionComponent.generated.h"

class ACharacter;
class UCharacterMovementComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRoverLocomotionStateChanged, ERoverLocomotionState, PreviousState, ERoverLocomotionState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRoverGaitChanged, ERoverGait, PreviousGait, ERoverGait, NewGait);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRoverLanded, ERoverLandingType, LandingType, float, ImpactSpeed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRoverSimpleLocomotionEvent);

UCLASS(ClassGroup = (Rover), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class ROVERREPLICA_API URoverLocomotionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URoverLocomotionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rover|Config")
	TObjectPtr<URoverMovementConfig> MovementConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rover|Config", meta = (EditCondition = "MovementConfig == nullptr", EditConditionHides))
	FRoverMovementSettings FallbackSettings;

	UPROPERTY(BlueprintAssignable, Category = "Rover|Events")
	FRoverLocomotionStateChanged OnLocomotionStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Rover|Events")
	FRoverGaitChanged OnGaitChanged;

	UPROPERTY(BlueprintAssignable, Category = "Rover|Events")
	FRoverLanded OnLanded;

	UPROPERTY(BlueprintAssignable, Category = "Rover|Events")
	FRoverSimpleLocomotionEvent OnSecondJump;

	UPROPERTY(BlueprintAssignable, Category = "Rover|Events")
	FRoverSimpleLocomotionEvent OnSprintStarted;

	UFUNCTION(BlueprintCallable, Category = "Rover|Input")
	void SetMoveInput(const FVector2D& Input, const FVector& WorldDirection);

	UFUNCTION(BlueprintCallable, Category = "Rover|Input")
	void SetSprintHeld(bool bHeld);

	UFUNCTION(BlueprintCallable, Category = "Rover|Input")
	void SetCrouchHeld(bool bHeld);

	UFUNCTION(BlueprintCallable, Category = "Rover|Movement")
	bool TryJump();

	UFUNCTION(BlueprintCallable, Category = "Rover|Movement")
	void StopJump();

	void HandleMovementModeChanged();
	void HandleGroundJumped();
	void HandleLanded(float ImpactSpeed, bool bSuppressLandingAnimation = false);

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	ERoverLocomotionState GetLocomotionState() const { return LocomotionState; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	ERoverGait GetGait() const { return Gait; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	ERoverGait GetLastMovingGait() const { return LastMovingGait; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	ERoverLandingType GetLandingType() const { return LandingType; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	float GetInputMagnitude() const { return MoveInput.Size(); }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	FVector2D GetMoveInput() const { return MoveInput; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	FVector GetMoveWorldDirection() const { return MoveWorldDirection; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	bool IsSprinting() const { return Gait == ERoverGait::Sprint; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	bool HasUsedSecondJump() const { return bSecondJumpUsed; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	int32 GetGroundJumpRequestId() const { return GroundJumpRequestId; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	bool DidGroundJumpStartFromRun() const { return bGroundJumpStartedFromRun; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	bool DidGroundJumpUseLeftFoot() const { return bGroundJumpUsedLeftFoot; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	bool DidSecondJumpMoveBackward() const { return bSecondJumpMovedBackward; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	bool CanAcceptMovementInput() const
	{
		return InputLockRemaining <= 0.0f &&
			!bGroundTurnPending && !bGroundTurnActive &&
			!bMoveStopPending && !bMoveStopActive &&
			CombatMovementRestrictionRequestId == 0;
	}

	bool TryBeginCombatMovementRestriction(int32 RequestId);
	bool TryBeginAirCombatMovementRestriction(
		int32 RequestId,
		float HorizontalVelocityScale);
	bool BeginAirCombatAscent(int32 RequestId, float AscentHeight, float AscentDuration);
	bool BeginAirCombatDescent(int32 RequestId, float DescentSpeed);
	bool TransferCombatMovementRestriction(int32 PreviousRequestId, int32 NewRequestId);
	void EndCombatMovementRestriction(int32 RequestId, bool bRestorePhysicsPush = true);
	bool StartCombatAttackAdvance(int32 RequestId, float Distance, float Duration);
	void CancelCombatAttackAdvance(int32 RequestId);

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	int32 GetCombatMovementRestrictionRequestId() const { return CombatMovementRestrictionRequestId; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	bool IsCombatAttackAdvanceActive() const;

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	bool HasActiveAnimationRootMotion() const;

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	int32 GetGroundTurnRequestId() const { return GroundTurnRequestId; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	ERoverGroundTurnType GetGroundTurnType() const { return GroundTurnType; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	ERoverGait GetGroundTurnEntryGait() const { return GroundTurnEntryGait; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	bool IsGroundTurnPending() const { return bGroundTurnPending; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	bool IsGroundTurnActive() const { return bGroundTurnActive; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	bool DoesGroundTurnRight() const { return bGroundTurnRight; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	bool ShouldGroundTurnResumeMovement() const { return bGroundTurnResumeMovement; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	bool IsRunTurnbackResumeWindowOpen() const { return bGroundTurnResumeWindowOpen; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	int32 GetMoveStopRequestId() const { return MoveStopRequestId; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	bool IsMoveStopPending() const { return bMoveStopPending; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	bool IsMoveStopActive() const { return bMoveStopActive; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	bool DoesMoveStopUseLeftVariant() const { return bMoveStopUseLeftVariant; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	ERoverGait GetMoveStopGait() const { return MoveStopGait; }

	UFUNCTION(BlueprintPure, Category = "Rover|State")
	bool ShouldMoveStopResumeMovement() const { return bMoveStopResumeMovement; }

	void AcknowledgeGroundTurn(int32 RequestId);
	void AcknowledgeRunTurnbackResumeWindow(int32 RequestId);
	void CompleteGroundTurn(int32 RequestId);
	void AcknowledgeMoveStop(int32 RequestId);
	void CompleteMoveStop(int32 RequestId);

	const FRoverMovementSettings& GetSettings() const;

protected:
	virtual void BeginPlay() override;

private:
	void ApplySettings();
	void UpdateLocomotionState();
	void UpdateGait();
	void UpdateGroundTurnWatchdog(float DeltaTime);
	void UpdateActiveRunTurnback();
	void UpdateGroundTurnRequest();
	void UpdateMoveStopWatchdog(float DeltaTime);
	void PublishLooseDebrisMovementField();
	bool ShouldRequestReleaseStop() const;
	void RequestReleaseStop();
	bool ResolveMoveStopUseLeftVariant() const;
	bool IsGroundTurnInputCompatible() const;
	float GetMaxSpeedForGait(ERoverGait InGait) const;
	void CancelGroundTurn();
	void CancelMoveStop();
	void ApplyCombatPhysicsPushScale(int32 RequestId);
	void RestoreCombatPhysicsPushScale(int32 RequestId);
	void UpdateCombatPhysicsPushRestore(float DeltaTime);
	void ClearCombatPhysicsPushRestoreState();
	void SetLocomotionState(ERoverLocomotionState NewState);
	void SetGait(ERoverGait NewGait);

	UPROPERTY(Transient)
	TObjectPtr<ACharacter> CharacterOwner;

	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> CharacterMovement;

	FVector2D MoveInput = FVector2D::ZeroVector;
	FVector MoveWorldDirection = FVector::ZeroVector;
	ERoverLocomotionState LocomotionState = ERoverLocomotionState::Grounded;
	ERoverGait Gait = ERoverGait::Idle;
	ERoverGait LastMovingGait = ERoverGait::Run;
	ERoverLandingType LandingType = ERoverLandingType::None;
	float CachedDownwardSpeed = 0.0f;
	float InputLockRemaining = 0.0f;
	float LandingStateRemaining = 0.0f;
	bool bSprintHeld = false;
	bool bCrouchHeld = false;
	bool bSecondJumpUsed = false;
	bool bGroundJumpStartedFromRun = false;
	bool bGroundJumpUsedLeftFoot = true;
	bool bNextGroundJumpUsesLeftFoot = true;
	bool bSecondJumpMovedBackward = false;
	int32 GroundJumpRequestId = 0;
	ERoverGroundTurnType GroundTurnType = ERoverGroundTurnType::None;
	FVector GroundTurnDesiredDirection = FVector::ZeroVector;
	FVector GroundTurnInertiaDirection = FVector::ZeroVector;
	float GroundTurnTargetYaw = 0.0f;
	float GroundTurnEntrySpeed = 0.0f;
	float GroundTurnInertiaDistanceApplied = 0.0f;
	float GroundTurnResumeWindowOpenedAt = 0.0f;
	float GroundTurnStateElapsed = 0.0f;
	int32 GroundTurnRequestId = 0;
	ERoverGait GroundTurnEntryGait = ERoverGait::Idle;
	bool bGroundTurnPending = false;
	bool bGroundTurnActive = false;
	bool bGroundTurnRight = true;
	bool bGroundTurnArmed = true;
	bool bGroundTurnResumeMovement = false;
	bool bGroundTurnResumeWindowOpen = false;
	bool bGroundTurnControlsOrientation = false;
	bool bGroundTurnSavedOrientRotationToMovement = true;
	bool bGroundTurnSavedUseControllerDesiredRotation = false;
	bool bGroundTurnInertiaBlocked = false;
	float MoveStopStateElapsed = 0.0f;
	int32 MoveStopRequestId = 0;
	ERoverGait MoveStopGait = ERoverGait::Idle;
	FVector MoveStopDesiredDirection = FVector::ZeroVector;
	bool bMoveStopPending = false;
	bool bMoveStopActive = false;
	bool bMoveStopUseLeftVariant = true;
	bool bNextMoveStopUsesLeftVariant = true;
	bool bMoveStopResumeMovement = false;
	int32 CombatMovementRestrictionRequestId = 0;
	int32 CombatAttackAdvanceRequestId = 0;
	uint16 CombatAttackRootMotionSourceId = 0;
	int32 CombatPhysicsPushScaleRequestId = 0;
	float CachedCombatInitialPushForceFactor = 0.0f;
	float CachedCombatPushForceFactor = 0.0f;
	float CachedCombatStandingDownwardForceScale = 0.0f;
	float CombatPhysicsRestoreElapsed = 0.0f;
	float CombatPhysicsRestoreStartInitialPushForceFactor = 0.0f;
	float CombatPhysicsRestoreStartPushForceFactor = 0.0f;
	float CombatPhysicsRestoreStartStandingDownwardForceScale = 0.0f;
	bool bCombatPhysicsRestoreActive = false;
	FVector PreviousLooseDebrisSampleLocation = FVector::ZeroVector;
	bool bHasPreviousLooseDebrisSample = false;
};
