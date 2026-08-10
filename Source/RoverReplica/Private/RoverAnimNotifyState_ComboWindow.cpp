#include "RoverAnimNotifyState_ComboWindow.h"

#include "Components/SkeletalMeshComponent.h"
#include "RoverAnimInstance.h"

FString URoverAnimNotifyState_ComboWindow::GetNotifyName_Implementation() const
{
	return TEXT("ComboWindow");
}

void URoverAnimNotifyState_ComboWindow::NotifyBegin(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (URoverAnimInstance* AnimInstance = MeshComp ? Cast<URoverAnimInstance>(MeshComp->GetAnimInstance()) : nullptr)
	{
		AnimInstance->HandleComboWindowStateBegin();
	}
}

void URoverAnimNotifyState_ComboWindow::NotifyEnd(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	if (URoverAnimInstance* AnimInstance = MeshComp ? Cast<URoverAnimInstance>(MeshComp->GetAnimInstance()) : nullptr)
	{
		AnimInstance->HandleComboWindowStateEnd();
	}
}
