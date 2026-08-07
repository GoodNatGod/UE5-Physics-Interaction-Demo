#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "RoverCombatTypes.h"
#include "RoverCharacter.generated.h"

class UCameraComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class UMotionWarpingComponent;
class URoverCombatComponent;
class URoverHealthComponent;
class URoverLocomotionComponent;
class URoverWorldSkillComponent;
class USkeletalMeshComponent;
class USpringArmComponent;
struct FInputActionValue;

enum class ERoverCameraFollowState : uint8
{
	Free,
	MoveLocked,
	RecenterDelay,
	Recentering,
};

UCLASS(Blueprintable)
class ROVERREPLICA_API ARoverCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ARoverCharacter();

	virtual void Tick(float DeltaSeconds) override;
	virtual void PawnClientRestart() override;
	virtual void NotifyControllerChanged() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void OnJumped_Implementation() override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode = 0) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintPure, Category = "Rover|Components")
	URoverLocomotionComponent* GetLocomotionComponent() const { return LocomotionComponent; }

	UFUNCTION(BlueprintPure, Category = "Rover|Components")
	URoverCombatComponent* GetCombatComponent() const { return CombatComponent; }

	UFUNCTION(BlueprintPure, Category = "Rover|Components")
	URoverHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "Rover|Components")
	URoverWorldSkillComponent* GetWorldSkillComponent() const { return WorldSkillComponent; }

	UFUNCTION(BlueprintPure, Category = "Rover|Components")
	USkeletalMeshComponent* GetCombatWeapon() const { return CombatWeapon; }

	UFUNCTION(BlueprintPure, Category = "Rover|Components")
	USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	UFUNCTION(BlueprintPure, Category = "Rover|Components")
	UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	UFUNCTION(BlueprintPure, Category = "Rover|Input")
	bool HasActiveInputMappings() const;

	UFUNCTION(BlueprintCallable, Category = "Rover|Combat")
	void ReceiveCombatHit(const FRoverCombatHit& Hit);

	UFUNCTION(BlueprintPure, Category = "Rover|Combat")
	bool IsCombatWeaponVisible() const;

	UFUNCTION(BlueprintPure, Category = "Rover|Combat")
	float GetCombatWeaponWorldLength() const;

	bool TryBeginCombatMovementRestriction(int32 RequestId);
	bool TransferCombatMovementRestriction(int32 PreviousRequestId, int32 NewRequestId);
	void EndCombatMovementRestriction(int32 RequestId, bool bRestorePhysicsPush = true);
	bool StartCombatAttackAdvance(int32 RequestId, float Distance, float Duration);
	void CancelCombatAttackAdvance(int32 RequestId);
	void PlayCombatAttackImmediately(int32 RequestId);
	void StopCombatAttack(int32 RequestId, float BlendOutTime);
	void SetCombatWeaponHand(ERoverWeaponHand WeaponHand);
	void SetCombatWeaponVisible(bool bVisible);
	bool GetWeaponTraceLocations(FVector& OutBase, FVector& OutTip) const;

	void HandleAttackStartedNotify(int32 RequestId);
	void HandleAttackActiveBeginNotify(int32 RequestId);
	void HandleAttackActiveEndNotify(int32 RequestId);
	void HandleComboWindowBeginNotify(int32 RequestId);
	void HandleComboWindowEndNotify(int32 RequestId);
	void HandleAttackRecoveryBeginNotify(int32 RequestId);
	void HandleAttackFinishedNotify(int32 RequestId);
	void HandleAttackMontageEnded(int32 RequestId, bool bInterrupted);
	void HandleAttackAnimationRejected(int32 RequestId);
	void HandleHitReactionStartedNotify(int32 RequestId);
	void HandleHitReactionFinishedNotify(int32 RequestId);
	void HandleHitReactionMontageEnded(int32 RequestId, bool bInterrupted);
	void HandleHitReactionAnimationRejected(int32 RequestId);

protected:
	virtual void BeginPlay() override;
	virtual void PostInitializeComponents() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rover|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rover|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rover|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<URoverLocomotionComponent> LocomotionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rover|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<URoverCombatComponent> CombatComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rover|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<URoverHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rover|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<URoverWorldSkillComponent> WorldSkillComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rover|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> CombatWeapon;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rover|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMotionWarpingComponent> MotionWarpingComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rover|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rover|Input")
	TObjectPtr<UInputMappingContext> MouseLookMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rover|Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rover|Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rover|Input")
	TObjectPtr<UInputAction> MouseLookAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rover|Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rover|Input")
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rover|Input")
	TObjectPtr<UInputAction> CrouchAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rover|Input")
	TObjectPtr<UInputAction> AttackAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rover|Input")
	TObjectPtr<UInputAction> FireballAction;

private:
	void EnsureRuntimeInputObjects();
	void AddInputMappingContexts();
	void RemoveInputMappingContexts();
	void HandleMove(const FInputActionValue& Value);
	void HandleMoveCompleted(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleJumpStarted();
	void HandleJumpCompleted();
	void HandleSprintStarted();
	void HandleSprintCompleted();
	void HandleCrouchStarted();
	void HandleCrouchCompleted();
	void HandleAttackStarted();
	void HandleAttackCompleted();
	void HandleFireballStarted();
	void ConfigureCombatWeapon();
	void UpdateCamera(float DeltaSeconds);
	void CancelCameraAutoFollow();
	void LockCameraForMove(const FVector2D& MoveInput, const FVector& WorldDirection);
	void QueueCameraRecenter();

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> RuntimeMappingContext;

	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InputSubsystemWithMappings;
	bool bAddedDefaultMappingContext = false;
	bool bAddedMouseLookMappingContext = false;
	bool bAddedRuntimeMappingContext = false;
	ERoverCameraFollowState CameraFollowState = ERoverCameraFollowState::Free;
	FVector LastCameraMoveWorldDirection = FVector::ZeroVector;
	float CameraRecenterTargetYaw = 0.0f;
	float CameraRecenterDelayRemaining = 0.0f;
	float CameraAutoFollowSuppressionRemaining = 0.0f;
};
