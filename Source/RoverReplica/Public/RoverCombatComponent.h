#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RoverCombatConfig.h"
#include "RoverCombatTypes.h"
#include "RoverCombatComponent.generated.h"

class ARoverCharacter;
class UAnimMontage;

UCLASS(ClassGroup = (Rover), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class ROVERREPLICA_API URoverCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URoverCombatComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rover|Config")
	TObjectPtr<URoverCombatConfig> CombatConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rover|Config", meta = (EditCondition = "CombatConfig == nullptr", EditConditionHides))
	FRoverCombatSettings FallbackSettings;

	UFUNCTION(BlueprintCallable, Category = "Rover|Combat")
	bool RequestAttack();

	UFUNCTION(BlueprintCallable, Category = "Rover|Combat")
	bool RequestAirAttack();

	UFUNCTION(BlueprintCallable, Category = "Rover|Combat")
	bool RequestHeavyAttack();

	UFUNCTION(BlueprintCallable, Category = "Rover|Combat")
	bool StartHeavyResonance(const FVector& AttackDirection);

	UFUNCTION(BlueprintCallable, Category = "Rover|Combat|Input")
	bool BeginAttackInput();

	UFUNCTION(BlueprintCallable, Category = "Rover|Combat|Input")
	bool EndAttackInput();

	UFUNCTION(BlueprintCallable, Category = "Rover|Combat")
	void SetLightAttackHeld(bool bHeld);

	UFUNCTION(BlueprintPure, Category = "Rover|Combat|Input")
	bool IsAttackInputDecisionPending() const { return bPendingAttackDecision; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat|Input")
	float GetAttackInputHoldElapsed() const { return LightAttackHoldElapsed; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat")
	ERoverCombatPhase GetCombatPhase() const { return CombatPhase; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat")
	ERoverAttackType GetCurrentAttackType() const { return CurrentAttackType; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat")
	ERoverAttackType GetPreviousAttackType() const { return PreviousAttackType; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat")
	bool IsAttacking() const { return CombatPhase != ERoverCombatPhase::None; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat")
	bool IsInHitReaction() const { return bInHitReaction; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat")
	int32 GetAttackRequestId() const { return ActiveAttackRequestId; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat")
	int32 GetCurrentComboIndex() const { return CurrentComboIndex; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat")
	int32 GetHitReactionRequestId() const { return HitReactionRequestId; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat")
	ERoverHitReactionType GetHitReactionType() const { return HitReactionType; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat")
	bool IsWeaponTraceActive() const { return bWeaponTraceActive; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat")
	bool IsComboWindowOpen() const { return bComboWindowOpen; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat|Air")
	bool HasAirAttackLanded() const { return bAirAttackLanded; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat|Resonance")
	bool IsResonanceWindowOpen() const { return bResonanceWindowOpen; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat|Resonance")
	bool IsResonanceTriggerWindowOpen() const { return bResonanceTriggerWindow; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat|Resonance")
	float GetResonanceTriggerRemaining() const { return ResonanceTriggerRemaining; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat")
	bool IsAttackInputBuffered() const { return bBufferedAttackInput; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat")
	float GetComboResetRemaining() const { return ComboResetRemaining; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat")
	FVector GetActiveAttackDirection() const { return ActiveAttackDirection; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat|Third Attack Throw")
	bool IsThirdAttackWeaponThrowActive() const
	{
		return ThirdAttackThrowPhase != ERoverThirdAttackThrowPhase::Inactive;
	}

	UFUNCTION(BlueprintPure, Category = "Rover|Combat|Third Attack Throw")
	ERoverThirdAttackThrowPhase GetThirdAttackWeaponThrowPhase() const { return ThirdAttackThrowPhase; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat|Third Attack Throw")
	FVector GetThirdAttackWeaponThrowAnchor() const { return ThirdAttackThrowAnchor.GetLocation(); }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat|Third Attack Throw")
	float GetThirdAttackWeaponThrowPhaseAlpha() const { return ThirdAttackThrowPhaseAlpha; }

	UFUNCTION(BlueprintCallable, Category = "Rover|Combat")
	bool RequestDodgeInterrupt();

	UFUNCTION(BlueprintCallable, Category = "Rover|Combat")
	bool RequestRecoveryMovementInterrupt();

	const FRoverCombatSettings& GetSettings() const;
	UAnimMontage* ResolveAttackMontage() const;
	float ResolveAttackPlayRate() const;
	float ResolveAttackTransitionBlendOutTime() const;
	UAnimMontage* ResolveHitReactionMontage() const;

	void AcknowledgeAttackStarted(int32 RequestId);
	void ReachAirAttackApex(int32 RequestId);
	void BeginAttackActive(int32 RequestId);
	void EndAttackActive(int32 RequestId);
	void BeginComboWindow(int32 RequestId);
	void EndComboWindow(int32 RequestId);
	void BeginResonanceWindow(int32 RequestId);
	void EndResonanceWindow(int32 RequestId);
	void BeginAttackRecovery(int32 RequestId);
	void FinishAttack(int32 RequestId);
	void HandleAttackMontageEnded(int32 RequestId, bool bInterrupted);
	void RejectAttackAnimation(int32 RequestId);
	bool HandleLanded(float ImpactSpeed);

	void HandleReceivedHit(const FRoverCombatHit& Hit);
	void AcknowledgeHitReactionStarted(int32 RequestId);
	void FinishHitReaction(int32 RequestId);
	void HandleHitReactionMontageEnded(int32 RequestId, bool bInterrupted);
	void RejectHitReactionAnimation(int32 RequestId);
	void HandleDeath();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	const FRoverAttackDefinition* GetAttackDefinition(int32 ComboIndex) const;
	const FRoverAttackDefinition* GetActiveAttackDefinition() const;
	float ResolveAirAttackAscentDuration() const;
	int32 GetLightAttackCount() const;
	FVector ResolveAttackInputDirection() const;
	bool CanStartHeavyResonance() const;
	bool StartAttackSegment(int32 ComboIndex, const FVector& AttackDirection, int32 PreviousRequestId = 0);
	bool StartFirstLightAttackAfterResonance(const FVector& AttackDirection);
	bool TransitionToNextAttack(const FVector& AttackDirection);
	void CompleteAttackSegment(int32 RequestId);
	void ResetAttackState(bool bResetCombo);
	void ResetComboState();
	void ResetAttackInputDecision();
	void CancelAttack(int32 RequestId, float MontageBlendOutTime = 0.0f);
	void CancelHitReaction(int32 RequestId);
	void EnableWeaponTrace();
	void DisableWeaponTrace();
	void PerformWeaponTrace();
	void ProcessTraceSegment(const FVector& Start, const FVector& End, const FVector& ImpactDirection);
	void BeginThirdAttackWeaponThrow(int32 RequestId);
	bool BeginThirdAttackWeaponOutbound();
	void BeginThirdAttackWeaponReturn();
	void UpdateThirdAttackWeaponThrow(float DeltaTime);
	void EndThirdAttackWeaponThrow(bool bSnapToHand, ERoverWeaponHand TargetHand = ERoverWeaponHand::Right);
	void RefreshThirdAttackWeaponThrowTrace();
	bool ShouldTraceThirdAttackWeaponThrow() const;
	float ResolveActiveTraceRadius(const FRoverAttackDefinition& Definition) const;
	int32 ResolveActiveTraceSampleCount(const FRoverAttackDefinition& Definition) const;
	float ResolveActiveTraceSubstepDistance(const FRoverAttackDefinition& Definition) const;
	int32 ResolveActiveMaxTraceSubsteps(const FRoverAttackDefinition& Definition) const;
	bool ShouldDrawAttackTrace() const;

	UPROPERTY(Transient)
	TObjectPtr<ARoverCharacter> CharacterOwner;

	ERoverCombatPhase CombatPhase = ERoverCombatPhase::None;
	ERoverAttackType CurrentAttackType = ERoverAttackType::None;
	ERoverAttackType PreviousAttackType = ERoverAttackType::None;
	ERoverHitReactionType HitReactionType = ERoverHitReactionType::None;
	TSet<TWeakObjectPtr<AActor>> HitActorsThisAttack;
	FVector PreviousTraceBase = FVector::ZeroVector;
	FVector PreviousTraceTip = FVector::ZeroVector;
	FVector ActiveAttackDirection = FVector::ZeroVector;
	FVector BufferedAttackDirection = FVector::ZeroVector;
	FVector ThirdAttackThrowSpinAxisWorld = FVector::RightVector;
	FTransform ThirdAttackThrowStart = FTransform::Identity;
	FTransform ThirdAttackThrowAnchor = FTransform::Identity;
	FTransform ThirdAttackThrowReturnStart = FTransform::Identity;
	float AttackWatchdogElapsed = 0.0f;
	float AttackInputBufferRemaining = 0.0f;
	float LightAttackHoldElapsed = 0.0f;
	float ComboResetRemaining = 0.0f;
	float ResonanceTriggerRemaining = 0.0f;
	float HitReactionWatchdogElapsed = 0.0f;
	float ThirdAttackThrowPhaseElapsed = 0.0f;
	float ThirdAttackThrowPhaseAlpha = 0.0f;
	float ThirdAttackThrowSpinDegrees = 0.0f;
	int32 AttackRequestSerial = 0;
	int32 ActiveAttackRequestId = 0;
	int32 CurrentComboIndex = -1;
	int32 HitReactionRequestSerial = 0;
	int32 HitReactionRequestId = 0;
	int32 ThirdAttackThrowRequestId = 0;
	ERoverThirdAttackThrowPhase ThirdAttackThrowPhase = ERoverThirdAttackThrowPhase::Inactive;
	bool bAttackAnimationAcknowledged = false;
	bool bLightAttackHeld = false;
	bool bPendingAttackDecision = false;
	bool bAttackInputRoutedImmediately = false;
	bool bAttackInputStartedFromThirdLightAttack = false;
	bool bAirAttackLanded = false;
	bool bBufferedAttackInput = false;
	bool bComboWindowOpen = false;
	bool bResonanceWindowOpen = false;
	bool bResonanceTriggerWindow = false;
	bool bWeaponTraceActive = false;
	bool bInHitReaction = false;
	bool bHitReactionAcknowledged = false;
	bool bDead = false;
};
