#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "WorldInteractionTypes.generated.h"

class AActor;
class AController;

UENUM(BlueprintType)
enum class EWorldElementType : uint8
{
	Physical,
	Fire,
	Wind,
	Water,
	Electric,
	Ice,
};

UENUM(BlueprintType)
enum class EWorldInteractionKind : uint8
{
	DirectHit,
	ProjectileImpact,
	Explosion,
	Environmental,
};

USTRUCT(BlueprintType)
struct ROVERREPLICA_API FWorldInteractionRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction")
	FGuid RequestId;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction")
	EWorldInteractionKind Kind = EWorldInteractionKind::DirectHit;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction")
	EWorldElementType Element = EWorldElementType::Physical;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction")
	TObjectPtr<AActor> SourceActor;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction")
	TObjectPtr<AController> InstigatorController;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction")
	FHitResult Hit;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction")
	FVector Origin = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction")
	FVector Direction = FVector::ForwardVector;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction", meta = (ClampMin = "0.0"))
	float Damage = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction", meta = (ClampMin = "0.0"))
	float ImpulseStrength = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction", meta = (ClampMin = "0.0"))
	float Radius = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction")
	TEnumAsByte<EPhysicalSurface> SurfaceType = SurfaceType_Default;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction")
	FGameplayTagContainer ContextTags;
};

USTRUCT(BlueprintType)
struct ROVERREPLICA_API FWorldInteractionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "World Interaction")
	bool bAccepted = false;

	UPROPERTY(BlueprintReadOnly, Category = "World Interaction")
	TEnumAsByte<EPhysicalSurface> SurfaceType = SurfaceType_Default;

	UPROPERTY(BlueprintReadOnly, Category = "World Interaction")
	int32 ReceiverCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "World Interaction")
	int32 PhysicsBodyCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "World Interaction")
	bool bSpawnedSurfaceFeedback = false;
};
