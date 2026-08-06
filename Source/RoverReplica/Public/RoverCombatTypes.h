#pragma once

#include "CoreMinimal.h"
#include "RoverCombatTypes.generated.h"

UENUM(BlueprintType)
enum class ERoverCombatPhase : uint8
{
	None,
	Startup,
	Active,
	ComboWindow,
	Recovery,
};

UENUM(BlueprintType)
enum class ERoverHitReactionType : uint8
{
	None,
	LightLeft,
	LightRight,
};

UENUM(BlueprintType)
enum class ERoverWeaponHand : uint8
{
	Right,
	Left,
};

USTRUCT(BlueprintType)
struct ROVERREPLICA_API FRoverCombatHit
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float Damage = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	float PoiseDamage = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FVector ImpactPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FVector SourceLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	int32 AttackRequestId = 0;
};
