#include "RoverHealthComponent.h"

URoverHealthComponent::URoverHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URoverHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	ResetHealth();
}

float URoverHealthComponent::ApplyCombatDamage(const float Damage)
{
	if (bDead || Damage <= 0.0f)
	{
		return 0.0f;
	}

	const float PreviousHealth = CurrentHealth;
	CurrentHealth = FMath::Clamp(CurrentHealth - Damage, 0.0f, MaxHealth);
	const float AppliedDamage = PreviousHealth - CurrentHealth;
	if (AppliedDamage <= 0.0f)
	{
		return 0.0f;
	}

	OnHealthChanged.Broadcast(PreviousHealth, CurrentHealth);
	if (CurrentHealth <= 0.0f)
	{
		bDead = true;
		OnDeath.Broadcast();
	}
	return AppliedDamage;
}

void URoverHealthComponent::ResetHealth()
{
	MaxHealth = FMath::Max(1.0f, InitialMaxHealth);
	CurrentHealth = MaxHealth;
	bDead = false;
}
