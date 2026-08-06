#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RoverHealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRoverHealthChanged, float, PreviousHealth, float, NewHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRoverDeathEvent);

UCLASS(ClassGroup = (Rover), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class ROVERREPLICA_API URoverHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URoverHealthComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rover|Health", meta = (ClampMin = "1.0"))
	float InitialMaxHealth = 500.0f;

	UPROPERTY(BlueprintAssignable, Category = "Rover|Health")
	FRoverHealthChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Rover|Health")
	FRoverDeathEvent OnDeath;

	UFUNCTION(BlueprintCallable, Category = "Rover|Health")
	float ApplyCombatDamage(float Damage);

	UFUNCTION(BlueprintCallable, Category = "Rover|Health")
	void ResetHealth();

	UFUNCTION(BlueprintPure, Category = "Rover|Health")
	float GetCurrentHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintPure, Category = "Rover|Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Rover|Health")
	bool IsDead() const { return bDead; }

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	float CurrentHealth = 0.0f;

	UPROPERTY(Transient)
	float MaxHealth = 0.0f;

	UPROPERTY(Transient)
	bool bDead = false;
};
