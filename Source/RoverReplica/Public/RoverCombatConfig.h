#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RoverCombatTypes.h"
#include "RoverCombatConfig.generated.h"

class UAnimMontage;
class USkeletalMesh;

USTRUCT(BlueprintType)
struct ROVERREPLICA_API FRoverAttackDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimMontage> Montage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	ERoverWeaponHand WeaponHand = ERoverWeaponHand::Right;

	// [PLACEHOLDER] Imported attacks are 66-110 frames long, so a modest speed-up
	// keeps the P0 chain responsive without baking timing into playback code.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0.1"))
	float AnimPlayRate = 1.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Blend", meta = (ClampMin = "0.0"))
	float MontageBlendInTime = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Blend", meta = (ClampMin = "0.0"))
	float MontageBlendOutTime = 0.10f;

	// UE stores this as seconds remaining, not as a normalized montage position.
	// Keep it aligned with BlendOutTime so the Finished notify is still reached.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Blend", meta = (ClampMin = "0.0"))
	float MontageBlendOutTriggerTime = 0.10f;

	// [PLACEHOLDER] Each attack segment can tune its own combo window after animation review.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Combo", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ComboWindowStartNormalized = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0"))
	float Damage = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0.0"))
	float PoiseDamage = 15.0f;

	// [PLACEHOLDER] Environment impulse is independent from character poise damage.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Interaction", meta = (ClampMin = "0.0"))
	float EnvironmentImpulseStrength = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trace", meta = (ClampMin = "0.0"))
	float TraceRadius = 10.0f;

	// [PLACEHOLDER] Number of points sampled from the blade base to the blade tip.
	// Base and tip are included, so values below two cannot describe the blade span.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trace", meta = (ClampMin = "2", ClampMax = "16"))
	int32 TraceSampleCount = 7;

	// [PLACEHOLDER] Maximum endpoint travel covered by one temporal trace step.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trace", meta = (ClampMin = "1.0"))
	float TraceSubstepDistance = 10.0f;

	// [PLACEHOLDER] Bounds collision query cost during unusually large frame-to-frame swings.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trace", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxTraceSubsteps = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float AdvanceDistance = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.05"))
	float AdvanceDuration = 0.28f;
};

USTRUCT(BlueprintType)
struct ROVERREPLICA_API FRoverCombatSettings
{
	GENERATED_BODY()

	FRoverCombatSettings();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Ground")
	TArray<FRoverAttackDefinition> LightAttackChain;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Input", meta = (ClampMin = "0.0"))
	float AttackInputBufferDuration = 0.25f;

	// [PLACEHOLDER] Keeps the next combo segment available briefly after a Montage fully ends.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Input", meta = (ClampMin = "0.0"))
	float ComboResetDuration = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Watchdog", meta = (ClampMin = "0.05"))
	float AttackPendingTimeout = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Watchdog", meta = (ClampMin = "0.1"))
	float AttackActiveTimeout = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitReaction|Watchdog", meta = (ClampMin = "0.1"))
	float HitReactionTimeout = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimMontage> LightHitLeftMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UAnimMontage> LightHitRightMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TSoftObjectPtr<USkeletalMesh> WeaponMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FName CharacterWeaponSocket = TEXT("RoverWeapon");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FName WeaponTraceBaseSocket = TEXT("WeaponTraceBase");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FName WeaponTraceTipSocket = TEXT("WeaponTraceTip");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FName ScabbardBone = TEXT("Scabbard_Bone001");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Transform")
	FVector WeaponRelativeLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Transform")
	FRotator WeaponRelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Transform")
	FVector WeaponRelativeScale = FVector(0.09f);

	// [PLACEHOLDER] Attack01 uses the source animation's left-hand grip. These
	// values are independent from the right-hand attachment used by Attack02/03.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Left Hand")
	FName LeftHandWeaponSocket = TEXT("Bip001LHand");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Left Hand")
	FVector LeftHandWeaponRelativeLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Left Hand")
	FRotator LeftHandWeaponRelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Left Hand")
	FVector LeftHandWeaponRelativeScale = FVector(0.09f);
};

UCLASS(BlueprintType)
class ROVERREPLICA_API URoverCombatConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	FRoverCombatSettings Settings;
};
