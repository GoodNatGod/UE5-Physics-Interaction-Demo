#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RoverMovementConfig.generated.h"

USTRUCT(BlueprintType)
struct ROVERREPLICA_API FRoverMovementSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", meta = (ClampMin = "1.0"))
	float CapsuleRadius = 34.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", meta = (ClampMin = "1.0"))
	float CapsuleHalfHeight = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground", meta = (ClampMin = "0.0"))
	float WalkSpeed = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground", meta = (ClampMin = "0.0"))
	float RunSpeed = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground", meta = (ClampMin = "0.0"))
	float SprintSpeed = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground", meta = (ClampMin = "0.0"))
	float MaxAcceleration = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground", meta = (ClampMin = "0.0"))
	float BrakingDeceleration = 1400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground", meta = (ClampMin = "0.0"))
	float GroundFriction = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground", meta = (ClampMin = "0.0"))
	float RotationRate = 720.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground|Movement End", meta = (ClampMin = "0.0"))
	float MoveStopWalkMinSpeed = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground|Movement End", meta = (ClampMin = "0.0"))
	float MoveStopRunMinSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground|Movement End", meta = (ClampMin = "0.0"))
	float MoveStopSprintMinSpeed = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground|Movement End", meta = (ClampMin = "0.1"))
	float MoveStopPendingTimeout = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground|Movement End", meta = (ClampMin = "0.1"))
	float MoveStopActiveTimeout = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground|Movement End", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float MoveStopResumeNormalizedTime = 0.52f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground|Movement End", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float MoveStopWalkResumeNormalizedTime = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground|Turn", meta = (ClampMin = "0.0"))
	float TurnInPlaceMaxSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground|Turn", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float TurnInPlaceAngle = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground|Turn", meta = (ClampMin = "0.0"))
	float RunTurnbackMinSpeed = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground|Turn", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float RunTurnbackAngle = 165.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground|Turn", meta = (ClampMin = "0.0"))
	float RunTurnbackInertiaDistance = 24.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground|Turn", meta = (ClampMin = "0.05"))
	float RunTurnbackInertiaDuration = 0.16f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground|Turn", meta = (ClampMin = "0.0"))
	float RunTurnbackRecoveryInitialSpeed = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground|Turn", meta = (ClampMin = "0.0"))
	float RunTurnbackRecoveryAcceleration = 4200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground|Turn", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float GroundTurnRearmAngle = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground|Turn", meta = (ClampMin = "0.1"))
	float GroundTurnPendingTimeout = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground|Turn", meta = (ClampMin = "0.1"))
	float GroundTurnActiveTimeout = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Idle", meta = (ClampMin = "0.0"))
	float IdleVariationMinTime = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Idle", meta = (ClampMin = "0.0"))
	float IdleVariationMaxTime = 30.0f;

	// [PLACEHOLDER] Ignore sub-centimeter foot offsets when choosing the post-attack idle stance.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Idle", meta = (ClampMin = "0.0"))
	float IdleFootStanceProjectionTolerance = 0.5f;

	// [PLACEHOLDER] Used for visible idle variations. Post-attack stance selection is
	// prepared immediately while the full-weight Montage still hides the locomotion pose.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Idle", meta = (ClampMin = "0.0"))
	float IdleStanceBlendTime = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InputDeadZone = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AnalogWalkThreshold = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SprintInputThreshold = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	bool bUseAnalogWalk = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (ClampMin = "0.0"))
	float JumpZVelocity = 630.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (ClampMin = "0.1"))
	float GravityScale = 1.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (ClampMin = "0.0"))
	float SecondJumpZVelocity = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (ClampMin = "0.0"))
	float SecondJumpHorizontalBoost = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirControl = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (ClampMin = "0.0"))
	float AirTurnRate = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (ClampMin = "0.0"))
	float JumpRunSpeedThreshold = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (ClampMin = "0.0"))
	float FastFallBlendStartSpeed = 650.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (ClampMin = "0.0"))
	float FastFallFullSpeed = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing", meta = (ClampMin = "0.0"))
	float LightLandingMaxSpeed = 650.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing", meta = (ClampMin = "0.0"))
	float HeavyLandingMaxSpeed = 1100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing", meta = (ClampMin = "0.0"))
	float LightLandingStateTime = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing", meta = (ClampMin = "0.0"))
	float HeavyLandingLockTime = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing", meta = (ClampMin = "0.0"))
	float RollLandingLockTime = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Landing", meta = (ClampMin = "0.0"))
	float RollLandingStateTime = 1.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "5.0", ClampMax = "170.0"))
	float DefaultFOV = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "5.0", ClampMax = "170.0"))
	float SprintFOV = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0"))
	float DefaultArmLength = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0"))
	float SprintArmLength = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0"))
	float CameraTransitionSpeed = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0"))
	float CameraLagSpeed = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0"))
	float CameraLagMaxDistance = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0"))
	float CameraAutoFollowDelay = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0"))
	float CameraAutoFollowInterpSpeed = 4.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0"))
	float CameraAutoFollowMaxYawSpeed = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float CameraAutoFollowYawTolerance = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0"))
	float CameraManualLookHoldTime = 0.35f;
};

UCLASS(BlueprintType)
class ROVERREPLICA_API URoverMovementConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	FRoverMovementSettings Settings;
};
