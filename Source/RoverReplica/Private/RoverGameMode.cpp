#include "RoverGameMode.h"

#include "RoverCharacter.h"

ARoverGameMode::ARoverGameMode()
{
	DefaultPawnClass = ARoverCharacter::StaticClass();
}
