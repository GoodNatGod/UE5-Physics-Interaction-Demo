#include "RoverAnimNotifyState_ResonanceWindow.h"

#include "Components/SkeletalMeshComponent.h"
#include "RoverAnimInstance.h"

FString URoverAnimNotifyState_ResonanceWindow::GetNotifyName_Implementation() const
{
	return TEXT("ResonanceWindow");
}

void URoverAnimNotifyState_ResonanceWindow::NotifyBegin(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (URoverAnimInstance* AnimInstance = MeshComp ? Cast<URoverAnimInstance>(MeshComp->GetAnimInstance()) : nullptr)
	{
		AnimInstance->HandleResonanceWindowStateBegin();
	}
}

void URoverAnimNotifyState_ResonanceWindow::NotifyEnd(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	if (URoverAnimInstance* AnimInstance = MeshComp ? Cast<URoverAnimInstance>(MeshComp->GetAnimInstance()) : nullptr)
	{
		AnimInstance->HandleResonanceWindowStateEnd();
	}
}
