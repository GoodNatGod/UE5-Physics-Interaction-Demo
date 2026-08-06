#pragma once

#include "CoreMinimal.h"
#include "RoverCharacter.h"
#include "RoverEnemyCharacter.generated.h"

UCLASS(Blueprintable)
class ROVERREPLICA_API ARoverEnemyCharacter : public ARoverCharacter
{
	GENERATED_BODY()

public:
	ARoverEnemyCharacter();
};
