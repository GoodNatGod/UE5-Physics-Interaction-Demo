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

UENUM(BlueprintType)
enum class EWorldLightweightInteractionSource : uint8
{
	Movement,
	Attack,
	Landing,
	Explosion,
	Wind,
	Jump,
};

UENUM(BlueprintType)
enum class EWorldLightweightInteractionShape : uint8
{
	Sphere,
	Capsule,
};

USTRUCT(BlueprintType)
struct ROVERREPLICA_API FWorldLightweightInteractionField
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction|Lightweight")
	int32 EventId = 0;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction|Lightweight")
	int32 SourceId = 0;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction|Lightweight")
	EWorldLightweightInteractionSource SourceType = EWorldLightweightInteractionSource::Movement;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction|Lightweight")
	EWorldLightweightInteractionShape ShapeType = EWorldLightweightInteractionShape::Sphere;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction|Lightweight")
	TObjectPtr<AActor> SourceActor;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction|Lightweight")
	FVector Start = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction|Lightweight")
	FVector End = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction|Lightweight")
	FVector Direction = FVector::ForwardVector;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction|Lightweight")
	FVector SourceVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction|Lightweight", meta = (ClampMin = "0.0", Units = "cm"))
	float Radius = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction|Lightweight", meta = (ClampMin = "0.0"))
	float Strength = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction|Lightweight", meta = (ClampMin = "0.0"))
	float UpwardLift = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction|Lightweight", meta = (ClampMin = "0.0", Units = "s"))
	float Duration = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction|Lightweight", meta = (ClampMin = "0.01"))
	float FalloffExponent = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category = "World Interaction|Lightweight")
	float SwirlStrength = 0.0f;

	FVector GetCenter() const { return (Start + End) * 0.5f; }
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
