#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WorldInteractionConfig.h"
#include "RoverWorldSkillComponent.generated.h"

class AWorldFireballProjectile;

UCLASS(ClassGroup = (Rover), BlueprintType, meta = (BlueprintSpawnableComponent))
class ROVERREPLICA_API URoverWorldSkillComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URoverWorldSkillComponent();

	UFUNCTION(BlueprintCallable, Category = "Rover|World Skill")
	bool RequestFireball();

	UFUNCTION(BlueprintPure, Category = "Rover|World Skill")
	const FWorldInteractionSettings& GetSettings() const;

	UFUNCTION(BlueprintPure, Category = "Rover|World Skill")
	AWorldFireballProjectile* GetLastSpawnedFireball() const { return LastSpawnedFireball.Get(); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rover|World Skill|Config")
	TObjectPtr<UWorldInteractionConfig> InteractionConfig;

protected:
	virtual void BeginPlay() override;

private:
	FWorldInteractionSettings FallbackSettings;
	TWeakObjectPtr<AWorldFireballProjectile> LastSpawnedFireball;
	float LastFireballTimeSeconds = 0.0f;
	bool bHasFiredFireball = false;
};
