import unreal


ASSET_ROOT = "/Game/Rover/Animations/P0"
ROOT_MOTION_NAMES = (
	"Land_Roll",
    "Stop_Walk_L",
    "Stop_Walk_R",
    "Stop_Run_L",
    "Stop_Run_R",
    "Stop_Sprint_L",
    "Stop_Sprint_R",
)


def _unpack_result(result):
    if isinstance(result, tuple):
        return bool(result[0]), str(result[1]) if len(result) > 1 else ""
    # Unreal's Python bridge uses the native bool return as success/failure
    # and exposes the FString output directly (or None when native returned false).
    return result is not None, "" if result is None else str(result)


def main():
    baked_assets = []
    for name in ROOT_MOTION_NAMES:
        sequence = unreal.load_asset(f"{ASSET_ROOT}/{name}")
        if not isinstance(sequence, unreal.AnimSequence):
            raise RuntimeError(f"Missing root-motion AnimSequence: {name}")

        succeeded, report = _unpack_result(
            unreal.RoverAnimationEditorLibrary.bake_move_stop_root_motion(sequence)
        )
        if not succeeded:
            raise RuntimeError(f"Failed to bake {name}: {report}")
        if not unreal.EditorAssetLibrary.save_loaded_asset(
            sequence, only_if_is_dirty=False
        ):
            raise RuntimeError(f"Failed to save baked root-motion asset: {name}")

        baked_assets.append(name)
        unreal.log(f"ROVER_ROOT_MOTION_BAKE name={name} {report}")

    unreal.log(
        "ROVER_ROOT_MOTION_BAKE_OK "
        f"count={len(baked_assets)} assets={','.join(baked_assets)}"
    )


if __name__ == "__main__":
    main()
