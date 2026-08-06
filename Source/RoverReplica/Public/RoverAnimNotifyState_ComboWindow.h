#pragma once

#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "RoverAnimNotifyState_ComboWindow.generated.h"

UCLASS(meta = (DisplayName = "ComboWindow"))
class ROVERREPLICA_API URoverAnimNotifyState_ComboWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual FString GetNotifyName_Implementation() const override;
	virtual void NotifyBegin(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
