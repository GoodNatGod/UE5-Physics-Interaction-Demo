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

	// Return true when the receiver applies its own rigid-body impulse for this request.
	// The subsystem will then skip its generic impulse for components owned by the receiver.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "World Interaction")
	bool HandlesWorldInteractionPhysicsImpulse(const FWorldInteractionRequest& Request) const;
	virtual bool HandlesWorldInteractionPhysicsImpulse_Implementation(
		const FWorldInteractionRequest& Request) const
	{
		return false;
	}
};
