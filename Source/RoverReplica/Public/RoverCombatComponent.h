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
	bool RequestLightAttack();

	UFUNCTION(BlueprintCallable, Category = "Rover|Combat")
	void SetLightAttackHeld(bool bHeld);

	UFUNCTION(BlueprintPure, Category = "Rover|Combat")
	ERoverCombatPhase GetCombatPhase() const { return CombatPhase; }

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

	UFUNCTION(BlueprintPure, Category = "Rover|Combat")
	bool IsAttackInputBuffered() const { return bBufferedAttackInput; }

	UFUNCTION(BlueprintPure, Category = "Rover|Combat")
	float GetComboResetRemaining() const { return ComboResetRemaining; }

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
	void BeginAttackActive(int32 RequestId);
	void EndAttackActive(int32 RequestId);
	void BeginComboWindow(int32 RequestId);
	void EndComboWindow(int32 RequestId);
	void BeginAttackRecovery(int32 RequestId);
	void FinishAttack(int32 RequestId);
	void HandleAttackMontageEnded(int32 RequestId, bool bInterrupted);
	void RejectAttackAnimation(int32 RequestId);

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
	int32 GetLightAttackCount() const;
	bool StartAttackSegment(int32 ComboIndex, int32 PreviousRequestId = 0);
	bool TransitionToNextAttack();
	void CompleteAttackSegment(int32 RequestId);
	void ResetAttackState(bool bResetCombo);
	void ResetComboState();
	void CancelAttack(int32 RequestId, float MontageBlendOutTime = 0.0f);
	void CancelHitReaction(int32 RequestId);
	void EnableWeaponTrace();
	void DisableWeaponTrace();
	void PerformWeaponTrace();
	void ProcessTraceSegment(const FVector& Start, const FVector& End, const FVector& ImpactDirection);

	UPROPERTY(Transient)
	TObjectPtr<ARoverCharacter> CharacterOwner;

	ERoverCombatPhase CombatPhase = ERoverCombatPhase::None;
	ERoverHitReactionType HitReactionType = ERoverHitReactionType::None;
	TSet<TWeakObjectPtr<AActor>> HitActorsThisAttack;
	FVector PreviousTraceBase = FVector::ZeroVector;
	FVector PreviousTraceTip = FVector::ZeroVector;
	float AttackWatchdogElapsed = 0.0f;
	float AttackInputBufferRemaining = 0.0f;
	float ComboResetRemaining = 0.0f;
	float HitReactionWatchdogElapsed = 0.0f;
	int32 AttackRequestSerial = 0;
	int32 ActiveAttackRequestId = 0;
	int32 CurrentComboIndex = -1;
	int32 HitReactionRequestSerial = 0;
	int32 HitReactionRequestId = 0;
	bool bAttackAnimationAcknowledged = false;
	bool bLightAttackHeld = false;
	bool bBufferedAttackInput = false;
	bool bComboWindowOpen = false;
	bool bWeaponTraceActive = false;
	bool bInHitReaction = false;
	bool bHitReactionAcknowledged = false;
	bool bDead = false;
};
