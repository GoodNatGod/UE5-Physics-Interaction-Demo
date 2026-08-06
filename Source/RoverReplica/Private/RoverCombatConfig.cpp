#include "RoverCombatConfig.h"

FRoverCombatSettings::FRoverCombatSettings()
	: LightHitLeftMontage(FSoftObjectPath(TEXT("/Game/Rover/Combat/Montages/AM_Rover_Behit_S_L.AM_Rover_Behit_S_L")))
	, LightHitRightMontage(FSoftObjectPath(TEXT("/Game/Rover/Combat/Montages/AM_Rover_Behit_S_R.AM_Rover_Behit_S_R")))
	, WeaponMesh(FSoftObjectPath(TEXT("/Game/Rover/Weapons/R2Sword001/SK_R2Sword001.SK_R2Sword001")))
{
	WeaponRelativeScale = FVector(0.09f);
	LightAttackChain.SetNum(3);
	LightAttackChain[0].Montage = FSoftObjectPath(TEXT("/Game/Rover/Combat/Montages/AM_Rover_Attack01.AM_Rover_Attack01"));
	LightAttackChain[0].WeaponHand = ERoverWeaponHand::Left;
	LightAttackChain[0].AnimPlayRate = 1.3f; // [PLACEHOLDER]
	LightAttackChain[0].Damage = 25.0f;
	LightAttackChain[0].PoiseDamage = 15.0f;
	LightAttackChain[0].EnvironmentImpulseStrength = 600.0f; // [PLACEHOLDER]
	LightAttackChain[0].TraceRadius = 10.0f; // [PLACEHOLDER]
	LightAttackChain[0].TraceSampleCount = 7; // [PLACEHOLDER]
	LightAttackChain[0].TraceSubstepDistance = 10.0f; // [PLACEHOLDER]
	LightAttackChain[0].MaxTraceSubsteps = 8; // [PLACEHOLDER]
	LightAttackChain[0].AdvanceDistance = 70.0f;
	LightAttackChain[0].AdvanceDuration = 0.28f;

	LightAttackChain[1].Montage = FSoftObjectPath(TEXT("/Game/Rover/Combat/Montages/AM_Rover_Attack02.AM_Rover_Attack02"));
	LightAttackChain[1].WeaponHand = ERoverWeaponHand::Right;
	LightAttackChain[1].AnimPlayRate = 1.3f; // [PLACEHOLDER]
	LightAttackChain[1].Damage = 30.0f;
	LightAttackChain[1].PoiseDamage = 20.0f;
	LightAttackChain[1].EnvironmentImpulseStrength = 750.0f; // [PLACEHOLDER]
	LightAttackChain[1].TraceRadius = 11.0f; // [PLACEHOLDER]
	LightAttackChain[1].TraceSampleCount = 7; // [PLACEHOLDER]
	LightAttackChain[1].TraceSubstepDistance = 10.0f; // [PLACEHOLDER]
	LightAttackChain[1].MaxTraceSubsteps = 8; // [PLACEHOLDER]
	LightAttackChain[1].AdvanceDistance = 85.0f;
	LightAttackChain[1].AdvanceDuration = 0.30f;

	LightAttackChain[2].Montage = FSoftObjectPath(TEXT("/Game/Rover/Combat/Montages/AM_Rover_Attack03.AM_Rover_Attack03"));
	LightAttackChain[2].WeaponHand = ERoverWeaponHand::Right;
	LightAttackChain[2].AnimPlayRate = 1.3f; // [PLACEHOLDER]
	LightAttackChain[2].Damage = 45.0f;
	LightAttackChain[2].PoiseDamage = 35.0f;
	LightAttackChain[2].EnvironmentImpulseStrength = 1000.0f; // [PLACEHOLDER]
	LightAttackChain[2].TraceRadius = 12.0f; // [PLACEHOLDER]
	LightAttackChain[2].TraceSampleCount = 7; // [PLACEHOLDER]
	LightAttackChain[2].TraceSubstepDistance = 10.0f; // [PLACEHOLDER]
	LightAttackChain[2].MaxTraceSubsteps = 8; // [PLACEHOLDER]
	LightAttackChain[2].AdvanceDistance = 110.0f;
	LightAttackChain[2].AdvanceDuration = 0.34f;
}
