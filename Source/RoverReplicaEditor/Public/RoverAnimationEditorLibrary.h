#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RoverAnimationEditorLibrary.generated.h"

class UBlendSpace;
class UAnimBlueprint;
class UAnimMontage;
class UAnimSequence;
class USkeletalMesh;

UCLASS()
class ROVERREPLICAEDITOR_API URoverAnimationEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Rover|Editor|Animation")
	static bool ResampleBlendSpace(UBlendSpace* BlendSpace);

	UFUNCTION(BlueprintPure, Category = "Rover|Editor|Animation")
	static int32 GetBlendSpaceRuntimeSegmentCount(const UBlendSpace* BlendSpace);

	UFUNCTION(BlueprintCallable, Category = "Rover|Editor|Animation")
	static bool BakeMoveStopRootMotion(
		UAnimSequence* AnimationSequence,
		FString& ValidationReport);

	UFUNCTION(BlueprintPure, Category = "Rover|Editor|Animation")
	static bool ValidateMoveStopRootMotion(
		const UAnimSequence* AnimationSequence,
		FString& ValidationReport);

	UFUNCTION(BlueprintCallable, Category = "Rover|Editor|Animation")
	static UAnimBlueprint* CreateRoverAnimBlueprint(
		USkeletalMesh* PreviewMesh,
		const FString& AssetPath = TEXT("/Game/Rover/Animations/ABP_Rover"));

	UFUNCTION(BlueprintCallable, Category = "Rover|Editor|Animation")
	static bool ValidateRoverAnimBlueprint(
		const UAnimBlueprint* AnimBlueprint,
		const USkeletalMesh* PreviewMesh,
		FString& ValidationReport);

	UFUNCTION(BlueprintCallable, Category = "Rover|Editor|Animation")
	static FString GetRoverAnimBlueprintValidationReport(
		const UAnimBlueprint* AnimBlueprint,
		const USkeletalMesh* PreviewMesh);

	UFUNCTION(BlueprintCallable, Category = "Rover|Editor|Combat")
	static bool CreateRoverCombatP0Assets(
		USkeletalMesh* RoverMesh,
		UAnimSequence* Attack01Sequence,
		UAnimSequence* Attack02Sequence,
		UAnimSequence* Attack03Sequence,
		UAnimSequence* LightHitLeftSequence,
		UAnimSequence* LightHitRightSequence,
		USkeletalMesh* WeaponMesh,
		FString& ValidationReport);

	UFUNCTION(BlueprintPure, Category = "Rover|Editor|Combat")
	static bool ValidateRoverAttackMontage(
		const UAnimSequence* AttackSequence,
		const UAnimMontage* AttackMontage,
		FString& ValidationReport);
};
