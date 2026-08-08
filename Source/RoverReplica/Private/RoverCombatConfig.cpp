#include "RoverCombatConfig.h"

FRoverCombatSettings::FRoverCombatSettings()
	: LightHitLeftMontage(FSoftObjectPath(TEXT("/Game/Rover/Combat/Montages/AM_Rover_Behit_S_L.AM_Rover_Behit_S_L")))
	, LightHitRightMontage(FSoftObjectPath(TEXT("/Game/Rover/Combat/Montages/AM_Rover_Behit_S_R.AM_Rover_Behit_S_R")))
	, WeaponMesh(FSoftObjectPath(TEXT("/Game/Rover/Weapons/R2Sword001/SK_R2Sword001.SK_R2Sword001")))
{
	WeaponRelativeScale = FVector(0.09f);
	LightAttackChain.SetNum(4);
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

	LightAttackChain[3].Montage = FSoftObjectPath(TEXT("/Game/Rover/Combat/Montages/AM_Rover_Attack04.AM_Rover_Attack04"));
	LightAttackChain[3].WeaponHand = ERoverWeaponHand::Right;
	LightAttackChain[3].AnimPlayRate = 1.3f; // [PLACEHOLDER]
	LightAttackChain[3].MontageBlendOutTime = 0.25f; // [PLACEHOLDER]
	LightAttackChain[3].MontageBlendOutTriggerTime = 0.25f; // [PLACEHOLDER]
	LightAttackChain[3].Damage = 40.0f; // [PLACEHOLDER]
	LightAttackChain[3].PoiseDamage = 30.0f; // [PLACEHOLDER]
	LightAttackChain[3].EnvironmentImpulseStrength = 900.0f; // [PLACEHOLDER]
	LightAttackChain[3].TraceRadius = 12.0f; // [PLACEHOLDER]
	LightAttackChain[3].TraceSampleCount = 7; // [PLACEHOLDER]
	LightAttackChain[3].TraceSubstepDistance = 10.0f; // [PLACEHOLDER]
	LightAttackChain[3].MaxTraceSubsteps = 8; // [PLACEHOLDER]
	LightAttackChain[3].AdvanceDistance = 60.0f; // [PLACEHOLDER]
	LightAttackChain[3].AdvanceDuration = 0.28f; // [PLACEHOLDER]

	AirAttackDefinition.Montage = FSoftObjectPath(TEXT("/Game/Rover/Combat/Montages/AM_Rover_AirAttack.AM_Rover_AirAttack"));
	AirAttackDefinition.WeaponHand = ERoverWeaponHand::Right;
	AirAttackDefinition.AnimPlayRate = 1.0f; // [PLACEHOLDER]
	AirAttackDefinition.MontageBlendInTime = 0.08f; // [PLACEHOLDER]
	AirAttackDefinition.MontageBlendOutTime = 0.15f; // [PLACEHOLDER]
	AirAttackDefinition.MontageBlendOutTriggerTime = 0.15f; // [PLACEHOLDER]
	AirAttackDefinition.Damage = 50.0f; // [PLACEHOLDER]
	AirAttackDefinition.PoiseDamage = 40.0f; // [PLACEHOLDER]
	AirAttackDefinition.EnvironmentImpulseStrength = 1200.0f; // [PLACEHOLDER]
	AirAttackDefinition.TraceRadius = 14.0f; // [PLACEHOLDER]
	AirAttackDefinition.TraceSampleCount = 7; // [PLACEHOLDER]
	AirAttackDefinition.TraceSubstepDistance = 10.0f; // [PLACEHOLDER]
	AirAttackDefinition.MaxTraceSubsteps = 8; // [PLACEHOLDER]
	AirAttackDefinition.AdvanceDistance = 0.0f;
	AirAttackDefinition.AdvanceDuration = 0.20f; // [PLACEHOLDER]

	HeavyAttackDefinition.Montage = FSoftObjectPath(TEXT("/Game/Rover/Combat/Montages/AM_Rover_Attack05.AM_Rover_Attack05"));
	HeavyAttackDefinition.WeaponHand = ERoverWeaponHand::Right;
	HeavyAttackDefinition.AnimPlayRate = 1.2f; // [PLACEHOLDER]
	HeavyAttackDefinition.Damage = 40.0f; // [PLACEHOLDER]
	HeavyAttackDefinition.PoiseDamage = 30.0f; // [PLACEHOLDER]
	HeavyAttackDefinition.EnvironmentImpulseStrength = 900.0f; // [PLACEHOLDER]
	HeavyAttackDefinition.TraceRadius = 12.0f; // [PLACEHOLDER]
	HeavyAttackDefinition.TraceSampleCount = 7; // [PLACEHOLDER]
	HeavyAttackDefinition.TraceSubstepDistance = 10.0f; // [PLACEHOLDER]
	HeavyAttackDefinition.MaxTraceSubsteps = 8; // [PLACEHOLDER]
	HeavyAttackDefinition.AdvanceDistance = 0.0f;
	HeavyAttackDefinition.AdvanceDuration = 0.30f; // [PLACEHOLDER]

	HeavyResonanceDefinition.Montage = FSoftObjectPath(TEXT("/Game/Rover/Combat/Montages/AM_Rover_Attack_EX01.AM_Rover_Attack_EX01"));
	HeavyResonanceDefinition.WeaponHand = ERoverWeaponHand::Right;
	HeavyResonanceDefinition.AnimPlayRate = 1.2f; // [PLACEHOLDER]
	HeavyResonanceDefinition.MontageBlendOutTime = 0.15f; // [PLACEHOLDER]
	HeavyResonanceDefinition.MontageBlendOutTriggerTime = 0.15f; // [PLACEHOLDER]
	HeavyResonanceDefinition.Damage = 55.0f; // [PLACEHOLDER]
	HeavyResonanceDefinition.PoiseDamage = 45.0f; // [PLACEHOLDER]
	HeavyResonanceDefinition.EnvironmentImpulseStrength = 1100.0f; // [PLACEHOLDER]
	HeavyResonanceDefinition.TraceRadius = 13.0f; // [PLACEHOLDER]
	HeavyResonanceDefinition.TraceSampleCount = 7; // [PLACEHOLDER]
	HeavyResonanceDefinition.TraceSubstepDistance = 10.0f; // [PLACEHOLDER]
	HeavyResonanceDefinition.MaxTraceSubsteps = 8; // [PLACEHOLDER]
	HeavyResonanceDefinition.AdvanceDistance = 400.0f; // [PLACEHOLDER]
	HeavyResonanceDefinition.AdvanceDuration = 0.25f; // [PLACEHOLDER]
}
