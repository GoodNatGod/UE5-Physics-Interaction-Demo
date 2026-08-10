#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldFireballProjectile.generated.h"

class UNiagaraComponent;
class UProjectileMovementComponent;
class USphereComponent;
class UWorldInteractionConfig;

UCLASS(Blueprintable)
class ROVERREPLICA_API AWorldFireballProjectile : public AActor
{
	GENERATED_BODY()

public:
	AWorldFireballProjectile();

	UFUNCTION(BlueprintCallable, Category = "World Interaction|Fireball")
	void InitializeProjectile(
		AActor* InSourceActor,
		const FVector& InLaunchDirection,
		UWorldInteractionConfig* InInteractionConfig);

	UFUNCTION(BlueprintCallable, Category = "World Interaction|Fireball")
	void DetonateAtHit(const FHitResult& Hit);

	UFUNCTION(BlueprintPure, Category = "World Interaction|Fireball")
	bool HasDetonated() const { return bDetonated; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Fireball")
	USphereComponent* GetCollisionSphere() const { return CollisionSphere; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Fireball")
	UProjectileMovementComponent* GetProjectileMovement() const { return ProjectileMovement; }

	UFUNCTION(BlueprintPure, Category = "World Interaction|Fireball")
	bool IsFireballEffectActive() const;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Interaction|Fireball")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Interaction|Fireball")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "World Interaction|Fireball")
	TObjectPtr<UNiagaraComponent> FireballEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Interaction|Config")
	TObjectPtr<UWorldInteractionConfig> InteractionConfig;

private:
	UFUNCTION()
	void HandleCollisionHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse,
		const FHitResult& Hit);

	UPROPERTY(Transient)
	TObjectPtr<AActor> SourceActor;

	FVector LaunchDirection = FVector::ForwardVector;
	bool bDetonated = false;
};
