#include "RoverEnemyCharacter.h"

#include "RoverHealthComponent.h"

ARoverEnemyCharacter::ARoverEnemyCharacter()
{
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	if (URoverHealthComponent* Health = GetHealthComponent())
	{
		Health->InitialMaxHealth = 300.0f;
	}
	Tags.Add(TEXT("RoverP0TrainingEnemy"));
}
