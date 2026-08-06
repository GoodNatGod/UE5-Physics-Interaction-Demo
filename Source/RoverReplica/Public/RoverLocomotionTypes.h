#pragma once

#include "CoreMinimal.h"
#include "RoverLocomotionTypes.generated.h"

UENUM(BlueprintType)
enum class ERoverLocomotionState : uint8
{
	Grounded,
	Airborne,
	Climbing,
	Hook,
	Swimming,
	Gliding,
	Sliding,
	Special
};

UENUM(BlueprintType)
enum class ERoverGait : uint8
{
	Idle,
	Walk,
	Run,
	Sprint
};

UENUM(BlueprintType)
enum class ERoverLandingType : uint8
{
	None,
	Light,
	Heavy,
	Roll
};

UENUM(BlueprintType)
enum class ERoverGroundTurnType : uint8
{
	None,
	TurnInPlace,
	RunTurnback
};
