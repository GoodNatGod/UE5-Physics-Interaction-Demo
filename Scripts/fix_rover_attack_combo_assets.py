import unreal


ASSETS = {
    "rover_mesh": ("/Game/Rover/Character/SK_Rover_Male", unreal.SkeletalMesh),
    "attack01": ("/Game/Rover/Combat/Animations/Attack01", unreal.AnimSequence),
    "attack02": ("/Game/Rover/Combat/Animations/Attack02", unreal.AnimSequence),
    "attack03": ("/Game/Rover/Combat/Animations/Attack03", unreal.AnimSequence),
    "hit_left": ("/Game/Rover/Combat/Animations/Behit_S_L", unreal.AnimSequence),
    "hit_right": ("/Game/Rover/Combat/Animations/Behit_S_R", unreal.AnimSequence),
    "weapon": (
        "/Game/Rover/Weapons/R2Sword001/SK_R2Sword001",
        unreal.SkeletalMesh,
    ),
}


def load_required_assets():
    loaded = {}
    for key, (path, expected_type) in ASSETS.items():
        asset = unreal.load_asset(path)
        if not isinstance(asset, expected_type):
            raise RuntimeError(f"Missing or invalid combat asset: {path}")
        loaded[key] = asset
    return loaded


def unpack_result(result):
    if isinstance(result, tuple):
        return bool(result[0]), str(result[-1])
    return bool(result), str(result)


def main():
    assets = load_required_assets()
    unreal.log(
        "ROVER_ATTACK_COMBO_FIX_SIGNATURE "
        + str(unreal.RoverAnimationEditorLibrary.create_rover_combat_p0_assets.__doc__)
    )
    result = unreal.RoverAnimationEditorLibrary.create_rover_combat_p0_assets(
        assets["rover_mesh"],
        assets["attack01"],
        assets["attack02"],
        assets["attack03"],
        assets["hit_left"],
        assets["hit_right"],
        assets["weapon"],
    )
    unreal.log(f"ROVER_ATTACK_COMBO_FIX_RESULT {result!r}")
    succeeded, report = unpack_result(result)
    if not succeeded:
        raise RuntimeError(f"Attack combo asset repair failed: {report}")
    weapon_socket = assets["rover_mesh"].find_socket("RoverWeapon")
    if weapon_socket is None:
        raise RuntimeError("RoverWeapon socket was not created on SK_Rover_Male")
    weapon_socket_bone = str(weapon_socket.get_editor_property("bone_name"))
    if weapon_socket_bone != "Bip001RHand":
        raise RuntimeError(
            "RoverWeapon socket must be bound to the right palm bone "
            f"Bip001RHand, got {weapon_socket_bone}"
        )
    unreal.log(
        f"ROVER_WEAPON_SOCKET socket=RoverWeapon bone={weapon_socket_bone}"
    )
    for index in range(1, 4):
        sequence = assets[f"attack0{index}"]
        montage = unreal.load_asset(
            f"/Game/Rover/Combat/Montages/AM_Rover_Attack0{index}"
        )
        validation = unreal.RoverAnimationEditorLibrary.validate_rover_attack_montage(
            sequence, montage
        )
        validation_succeeded, validation_report = unpack_result(validation)
        if not validation_succeeded:
            raise RuntimeError(
                f"Attack0{index} asset validation failed: {validation_report}"
            )
        unreal.log(
            f"ROVER_ATTACK_COMBO_MONTAGE index={index} length={montage.get_play_length():.6f} "
            f"blend_in={montage.get_default_blend_in_time():.3f} "
            f"blend_out={montage.get_default_blend_out_time():.3f} "
            f"trigger={montage.get_editor_property('blend_out_trigger_time'):.3f} "
            f"validation={validation_report}"
        )
    if not unreal.EditorAssetLibrary.save_directory(
        "/Game/Rover", only_if_is_dirty=True, recursive=True
    ):
        raise RuntimeError("Failed to save repaired Rover combat assets.")
    unreal.log(f"ROVER_ATTACK_COMBO_ASSETS_FIXED {report}")


main()
