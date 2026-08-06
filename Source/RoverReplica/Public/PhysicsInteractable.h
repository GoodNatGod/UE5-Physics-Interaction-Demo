#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "WorldInteractionTypes.h"
#include "PhysicsInteractable.generated.h"

UINTERFACE(BlueprintType)
class ROVERREPLICA_API UPhysicsInteractable : public UInterface
{
	GENERATED_BODY()
};

class ROVERREPLICA_API IPhysicsInteractable
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "World Interaction")
	bool CanHandleWorldInteraction(const FWorldInteractionRequest& Request) const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "World Interaction")
	void HandleWorldInteraction(const FWorldInteractionRequest& Request);
};
