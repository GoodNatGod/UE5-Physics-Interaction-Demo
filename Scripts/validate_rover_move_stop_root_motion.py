import unreal


ASSET_ROOT = "/Game/Rover/Animations/P0"
STOP_NAMES = (
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
    return result is not None, "" if result is None else str(result)


def main():
    reports = []
    for name in STOP_NAMES:
        sequence = unreal.load_asset(f"{ASSET_ROOT}/{name}")
        if not isinstance(sequence, unreal.AnimSequence):
            raise RuntimeError(f"Missing move-stop AnimSequence: {name}")

        succeeded, report = _unpack_result(
            unreal.RoverAnimationEditorLibrary.validate_move_stop_root_motion(sequence)
        )
        if not succeeded:
            raise RuntimeError(f"Move-stop root validation failed for {name}: {report}")
        reports.append(f"{name}[{report}]")
        unreal.log(f"ROVER_MOVE_STOP_ROOT_VALID name={name} {report}")

    unreal.log(
        "ROVER_MOVE_STOP_ROOT_VALIDATION_OK "
        f"count={len(reports)} reports={' | '.join(reports)}"
    )


if __name__ == "__main__":
    main()
