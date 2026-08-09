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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Air")
	FRoverAttackDefinition AirAttackDefinition;

	// [PLACEHOLDER] Extra height gained from AirAttack_Start before the plunge begins.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Air|Movement", meta = (ClampMin = "0.0", Units = "cm"))
	float AirAttackAscentHeight = 60.0f;

	// Source-animation frame where AirAttack_Start reaches its apex and begins the plunge.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Air|Movement", meta = (ClampMin = "1"))
	int32 AirAttackApexFrame = 10;

	// [PLACEHOLDER] Horizontal momentum retained when the plunge begins.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Air|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirAttackHorizontalVelocityScale = 0.45f;

	// [PLACEHOLDER] Downward speed applied when AirAttack_Start reaches its apex notify.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Air|Movement", meta = (ClampMin = "0.0"))
	float AirAttackDescentSpeed = 1600.0f;

	// [PLACEHOLDER] Watchdog for unusually high falls while the Loop section repeats.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Air|Watchdog", meta = (ClampMin = "0.1"))
	float AirAttackMaximumDuration = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Heavy")
	FRoverAttackDefinition HeavyAttackDefinition;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Resonance")
	FRoverAttackDefinition HeavyResonanceDefinition;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Input", meta = (ClampMin = "0.0"))
	float AttackInputBufferDuration = 0.25f;

	// [PLACEHOLDER] Idle presses defer the light attack until release. Combo presses route
	// immediately while continuing hold tracking so they can still promote to a heavy attack.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Input", meta = (ClampMin = "0.05", ClampMax = "0.3"))
	float HeavyAttackHoldThreshold = 0.12f;

	// [PLACEHOLDER] Heavy attacks retreat opposite the selected attack direction.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Heavy|Movement", meta = (ClampMin = "0.0"))
	float HeavyAttackRetreatDistance = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Heavy|Movement", meta = (ClampMin = "0.05"))
	float HeavyAttackRetreatDuration = 0.30f;

	// [PLACEHOLDER] Fraction of Attack03's ComboWindow reserved for Attack04 before
	// the later portion switches the same input to Heavy Resonance.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Resonance|Timing", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ResonanceHalfWindowNormalized = 0.5f;

	// [PLACEHOLDER] Follow-up grace after Attack03 or HeavyAttack fully ends.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Resonance|Timing", meta = (ClampMin = "0.0"))
	float ResonanceTriggerWindowDuration = 0.40f;

	// [PLACEHOLDER] Heavy Resonance uses a dedicated forward dash rather than the
	// generic per-definition advance so designers can tune the chain as one move.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Resonance|Movement", meta = (ClampMin = "0.0"))
	float ResonanceDashDistance = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Resonance|Movement", meta = (ClampMin = "0.05"))
	float ResonanceDashDuration = 0.25f;

	// Samples the current movement direction on every light-attack press so each
	// combo segment can face independently from the previous attack.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Direction")
	bool bAllowDirectionalLightAttack = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Third Throw")
	bool bEnableThirdAttackWeaponThrow = true;

	// [PLACEHOLDER] Delay from the AttackStarted notify until the sword leaves the right hand.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Third Throw|Timing", meta = (ClampMin = "0.0"))
	float ThirdAttackThrowStartDelay = 0.08f;

	// [PLACEHOLDER] Travel time from the right hand to the fixed forward target.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Third Throw|Timing", meta = (ClampMin = "0.01"))
	float ThirdAttackThrowOutboundDuration = 0.35f;

	// [PLACEHOLDER] Time spent spinning at the fixed world-space target.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Third Throw|Timing", meta = (ClampMin = "0.0"))
	float ThirdAttackThrowSpinDuration = 0.55f;

	// [PLACEHOLDER] Travel time from the fixed target back to the live left-hand socket.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Third Throw|Timing", meta = (ClampMin = "0.01"))
	float ThirdAttackThrowReturnDuration = 0.25f;

	// Offsets are expressed in combat space: forward follows the selected Attack03 direction,
	// lateral follows character right, and height follows world up.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Third Throw|Target")
	float ThirdAttackThrowTargetForwardOffset = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Third Throw|Target")
	float ThirdAttackThrowTargetLateralOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Third Throw|Target")
	float ThirdAttackThrowTargetHeightOffset = 80.0f;

	// [PLACEHOLDER] Applied after the release pose is captured.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Third Throw|Rotation")
	FRotator ThirdAttackThrowAnchorRotationOffset = FRotator::ZeroRotator;

	// Axis is expressed in combat space (X=attack forward, Y=character right, Z=world up).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Third Throw|Rotation")
	FVector ThirdAttackThrowSpinAxis = FVector::RightVector;

	// [PLACEHOLDER] Positive and negative values reverse the spin direction.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Third Throw|Rotation")
	float ThirdAttackThrowSpinDegreesPerSecond = 1080.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Third Throw|Collision")
	bool bThirdAttackThrowCollisionOutbound = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Third Throw|Collision")
	bool bThirdAttackThrowCollisionSpinning = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Third Throw|Collision")
	bool bThirdAttackThrowCollisionReturning = true;

	// [PLACEHOLDER] Independent from Attack03's hand-held trace radius.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Third Throw|Collision", meta = (ClampMin = "0.0"))
	float ThirdAttackThrowTraceRadius = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Third Throw|Collision", meta = (ClampMin = "2", ClampMax = "24"))
	int32 ThirdAttackThrowTraceSampleCount = 9;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Third Throw|Collision", meta = (ClampMin = "1.0"))
	float ThirdAttackThrowTraceSubstepDistance = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Third Throw|Collision", meta = (ClampMin = "1", ClampMax = "24"))
	int32 ThirdAttackThrowMaxTraceSubsteps = 12;

	// [PLACEHOLDER] Keeps the next combo segment available briefly after a Montage fully ends.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Input", meta = (ClampMin = "0.0"))
	float ComboResetDuration = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Watchdog", meta = (ClampMin = "0.05"))
	float AttackPendingTimeout = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Watchdog", meta = (ClampMin = "0.1"))
	float AttackActiveTimeout = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HitReaction|Watchdog", meta = (ClampMin = "0.1"))
	float HitReactionTimeout = 2.0f;

	// Draws only the weapon trace visualization. Collision queries and damage remain active when disabled.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Debug")
	bool bDrawAttackTrace = false;

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
