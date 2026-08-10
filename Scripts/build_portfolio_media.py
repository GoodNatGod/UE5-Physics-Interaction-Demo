import argparse
from pathlib import Path

from PIL import Image, ImageOps


CAPTURE_DURATION_SECONDS = 8.5
GIF_FPS = 10.0
GIF_SIZE = (720, 405)
HERO_SIZE = (1600, 900)


def fit_16_by_9(image, size):
    return ImageOps.fit(
        image,
        size,
        method=Image.Resampling.LANCZOS,
        centering=(0.5, 0.5),
    )


def load_frame_paths(frame_directory):
    paths = sorted(frame_directory.glob("*.bmp"))
    if not paths:
        paths = sorted(frame_directory.glob("*.png"))
    if len(paths) < 2:
        raise RuntimeError(f"No captured bitmap sequence found in {frame_directory}")
    return paths


def sampled_indices(frame_count, capture_duration):
    output_count = max(2, round(capture_duration * GIF_FPS))
    return [
        round(index * (frame_count - 1) / (output_count - 1))
        for index in range(output_count)
    ]


def gif_frame_durations(frame_count, capture_duration):
    total_ticks = round(capture_duration * 100.0)
    return [
        10
        * (
            ((index + 1) * total_ticks // frame_count)
            - (index * total_ticks // frame_count)
        )
        for index in range(frame_count)
    ]


def build_global_palette(paths, indices):
    sample_count = min(12, len(indices))
    sample_indices = [
        indices[round(index * (len(indices) - 1) / max(sample_count - 1, 1))]
        for index in range(sample_count)
    ]
    thumbnails = []
    for frame_index in sample_indices:
        with Image.open(paths[frame_index]) as image:
            thumbnail = image.convert("RGB")
            thumbnail.thumbnail((240, 135), Image.Resampling.LANCZOS)
            thumbnails.append(thumbnail.copy())
    sheet = Image.new("RGB", (240 * len(thumbnails), 135))
    for index, thumbnail in enumerate(thumbnails):
        sheet.paste(thumbnail, (index * 240, 0))
    return sheet.quantize(colors=128, method=Image.Quantize.MEDIANCUT)


def build_gif(paths, output_path, capture_duration):
    indices = sampled_indices(len(paths), capture_duration)
    durations = gif_frame_durations(len(indices), capture_duration)
    palette = build_global_palette(paths, indices)
    frames = []
    for frame_index in indices:
        with Image.open(paths[frame_index]) as image:
            frame = fit_16_by_9(image.convert("RGB"), GIF_SIZE)
            frames.append(
                frame.quantize(
                    palette=palette,
                    dither=Image.Dither.FLOYDSTEINBERG,
                )
            )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    frames[0].save(
        output_path,
        save_all=True,
        append_images=frames[1:],
        duration=durations,
        loop=0,
        optimize=True,
        disposal=2,
    )
    return len(frames), sum(durations) / 1000.0


def build_hero(paths, output_path, hero_time, capture_duration):
    normalized_time = max(0.0, min(1.0, hero_time / capture_duration))
    frame_index = round(normalized_time * (len(paths) - 1))
    with Image.open(paths[frame_index]) as image:
        hero = fit_16_by_9(image.convert("RGB"), HERO_SIZE)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        hero.save(output_path, format="PNG", optimize=True, compress_level=9)
    return frame_index


def main():
    parser = argparse.ArgumentParser(
        description="Build GitHub README media from an Unreal PNG capture sequence."
    )
    parser.add_argument("frame_directory", type=Path)
    parser.add_argument("output_directory", type=Path)
    parser.add_argument(
        "--capture-duration",
        type=float,
        default=CAPTURE_DURATION_SECONDS,
        help="Recorded timeline duration in seconds (default: 8.5).",
    )
    parser.add_argument(
        "--hero-time",
        type=float,
        default=4.2,
        help="Recorded time to use for hero.png (default: 4.2s).",
    )
    args = parser.parse_args()

    paths = load_frame_paths(args.frame_directory.resolve())
    if args.capture_duration <= 0.0:
        raise ValueError("--capture-duration must be greater than zero")
    output_directory = args.output_directory.resolve()
    frame_count, duration = build_gif(
        paths,
        output_directory / "demo.gif",
        args.capture_duration,
    )
    hero_frame = build_hero(
        paths,
        output_directory / "hero.png",
        args.hero_time,
        args.capture_duration,
    )
    print(
        "PORTFOLIO_MEDIA_OK "
        f"source_frames={len(paths)} gif_frames={frame_count} "
        f"duration={duration:.2f}s hero_frame={hero_frame} "
        f"output={output_directory}"
    )


if __name__ == "__main__":
    main()
