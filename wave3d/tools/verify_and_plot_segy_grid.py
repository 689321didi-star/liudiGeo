#!/usr/bin/env python3
"""Stream-verify a Wave3D square receiver grid and render diagnostic maps."""

from __future__ import annotations

import argparse
import base64
import hashlib
import html
import math
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
from plot_segy import png_bytes, percentile  # noqa: E402


COMPONENT_CODES = (14, 13, 12)
COMPONENT_NAMES = ("VX", "VY", "VZ")
VIRIDIS = (
    (0.00, (68, 1, 84)),
    (0.25, (59, 82, 139)),
    (0.50, (33, 145, 140)),
    (0.75, (94, 201, 98)),
    (1.00, (253, 231, 37)),
)


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from(">H", data, offset)[0]


def i16(data: bytes, offset: int) -> int:
    return struct.unpack_from(">h", data, offset)[0]


def i32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">i", data, offset)[0]


def scaled(stored: int, scalar: int) -> float:
    if scalar == 0:
        raise ValueError("SEG-Y coordinate scalar is zero")
    return stored / abs(scalar) if scalar < 0 else stored * scalar


def map_color(fraction: float) -> tuple[int, int, int]:
    fraction = max(0.0, min(1.0, fraction))
    for (left_x, left), (right_x, right) in zip(VIRIDIS, VIRIDIS[1:]):
        if fraction <= right_x:
            weight = (fraction - left_x) / (right_x - left_x)
            return tuple(round(a + (b - a) * weight) for a, b in zip(left, right))
    return VIRIDIS[-1][1]


def render_map(values: list[float], side: int, minimum: float, maximum: float) -> bytes:
    if len(values) != side * side or not maximum > minimum:
        raise ValueError("invalid receiver-map range")
    pixels = bytearray(side * side * 3)
    for index, value in enumerate(values):
        if not math.isfinite(value):
            raise ValueError("receiver map contains a non-finite value")
        pixels[index * 3 : index * 3 + 3] = bytes(
            map_color((value - minimum) / (maximum - minimum))
        )
    return png_bytes(side, side, bytes(pixels))


def data_uri(png: bytes) -> str:
    return "data:image/png;base64," + base64.b64encode(png).decode("ascii")


def maps_svg(
    input_path: Path,
    side: int,
    peak_maps: list[list[float]],
    arrivals: list[float],
    peak_clip: float,
    arrival_range: tuple[float, float],
    x_range: tuple[float, float],
    y_range: tuple[float, float],
    source_xy: tuple[float, float],
) -> str:
    canvas_width, canvas_height = 860, 900
    panel_size = 320
    positions = ((70, 80), (470, 80), (70, 480), (470, 480))
    normalized_peaks = [
        [math.sqrt(min(value / peak_clip, 1.0)) for value in values]
        for values in peak_maps
    ]
    images = [
        render_map(values, side, 0.0, 1.0) for values in normalized_peaks
    ]
    images.append(render_map(arrivals, side, *arrival_range))
    labels = (
        "VX peak |particle velocity|",
        "VY peak |particle velocity|",
        "VZ peak |particle velocity|",
        "first significant arrival time",
    )
    stops = "".join(
        f'<stop offset="{position * 100:g}%" stop-color="rgb{color}"/>'
        for position, color in VIRIDIS
    )
    parts = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        (
            f'<svg xmlns="http://www.w3.org/2000/svg" '
            f'xmlns:xlink="http://www.w3.org/1999/xlink" width="{canvas_width}" '
            f'height="{canvas_height}" viewBox="0 0 {canvas_width} {canvas_height}">'
        ),
        '<rect width="100%" height="100%" fill="white"/>',
        f'<defs><linearGradient id="viridis">{stops}</linearGradient></defs>',
        (
            f'<text x="{canvas_width / 2:g}" y="30" text-anchor="middle" '
            'font-family="sans-serif" font-size="21" font-weight="600">'
            'Wave3D dense surface-receiver diagnostics</text>'
        ),
        (
            f'<text x="{canvas_width / 2:g}" y="51" text-anchor="middle" '
            'font-family="sans-serif" font-size="12" fill="#555">'
            f'{html.escape(input_path.name)} · {side}×{side} receivers · '
            'x fastest within each y row</text>'
        ),
    ]
    for index, ((x, y), image, label) in enumerate(zip(positions, images, labels)):
        parts.extend(
            [
                f'<text x="{x + panel_size / 2:g}" y="{y - 10}" text-anchor="middle" font-family="sans-serif" font-size="14" font-weight="600">{label}</text>',
                f'<image x="{x}" y="{y}" width="{panel_size}" height="{panel_size}" image-rendering="pixelated" xlink:href="{data_uri(image)}"/>',
                f'<rect x="{x}" y="{y}" width="{panel_size}" height="{panel_size}" fill="none" stroke="#222"/>',
                f'<text x="{x + panel_size / 2:g}" y="{y + panel_size + 19}" text-anchor="middle" font-family="sans-serif" font-size="11">x: {x_range[0]:g}–{x_range[1]:g} m</text>',
                f'<text x="{x - 38}" y="{y + panel_size / 2:g}" transform="rotate(-90 {x - 38} {y + panel_size / 2:g})" text-anchor="middle" font-family="sans-serif" font-size="11">y: {y_range[0]:g}–{y_range[1]:g} m</text>',
            ]
        )
        source_x = x + (source_xy[0] - x_range[0]) / (x_range[1] - x_range[0]) * panel_size
        source_y = y + (source_xy[1] - y_range[0]) / (y_range[1] - y_range[0]) * panel_size
        parts.append(
            f'<circle cx="{source_x:g}" cy="{source_y:g}" r="5" fill="none" stroke="white" stroke-width="1.5"/>'
        )
    parts.extend(
        [
            '<rect x="145" y="843" width="230" height="15" fill="url(#viridis)" stroke="#333"/>',
            '<text x="145" y="877" font-family="sans-serif" font-size="10">0</text>',
            '<text x="375" y="877" text-anchor="end" font-family="sans-serif" font-size="10">clip</text>',
            f'<text x="260" y="877" text-anchor="middle" font-family="sans-serif" font-size="10">sqrt scale; shared P99.5 clip={peak_clip:.4e} m/s</text>',
            '<rect x="545" y="843" width="230" height="15" fill="url(#viridis)" stroke="#333"/>',
            f'<text x="545" y="877" font-family="sans-serif" font-size="10">{arrival_range[0]:.3f} s</text>',
            f'<text x="775" y="877" text-anchor="end" font-family="sans-serif" font-size="10">{arrival_range[1]:.3f} s</text>',
        ]
    )
    parts.append("</svg>")
    return "\n".join(parts) + "\n"


def verify_and_collect(arguments: argparse.Namespace) -> tuple[dict[str, object], list[list[float]], list[float]]:
    path = arguments.input
    file_bytes = path.stat().st_size
    with path.open("rb") as source:
        file_header = source.read(3600)
        if len(file_header) != 3600:
            raise ValueError("file is shorter than the SEG-Y headers")
        sample_count = u16(file_header, 3220)
        dt_us = u16(file_header, 3216)
        format_code = u16(file_header, 3224)
        if format_code != 5 or sample_count == 0 or dt_us == 0:
            raise ValueError("expected fixed big-endian IEEE float32 SEG-Y traces")
        receiver_count = arguments.side * arguments.side
        trace_count = receiver_count * 3
        trace_bytes = 240 + 4 * sample_count
        expected_bytes = 3600 + trace_count * trace_bytes
        if file_bytes != expected_bytes:
            raise ValueError(f"file has {file_bytes} bytes; expected {expected_bytes}")
        if i16(file_header, 3212) != trace_count:
            raise ValueError("binary-header trace count changed")

        digest = hashlib.sha256()
        digest.update(file_header)
        peaks = [[], [], []]
        energy_parts = [[], [], []]
        arrivals = []
        finite_count = 0
        nonzero_count = 0
        maximum_absolute = 0.0
        arrivals_in_bounds = 0
        polarity_matches = 0
        polarity_total = 0
        dt_s = dt_us * 1.0e-6
        unpack_format = f">{sample_count}f"

        for receiver in range(receiver_count):
            ix, iy = receiver % arguments.side, receiver // arguments.side
            expected_x = arguments.x0 + ix * arguments.spacing
            expected_y = arguments.y0 + iy * arguments.spacing
            components = []
            envelope = [0.0] * sample_count
            for component, expected_code in enumerate(COMPONENT_CODES):
                trace = receiver * 3 + component
                trace_header = source.read(240)
                sample_bytes = source.read(4 * sample_count)
                if len(trace_header) != 240 or len(sample_bytes) != 4 * sample_count:
                    raise ValueError(f"trace {trace + 1} is truncated")
                digest.update(trace_header)
                digest.update(sample_bytes)
                sequence = trace + 1
                if tuple(i32(trace_header, field) for field in (0, 4, 12, 24)) != (sequence,) * 4:
                    raise ValueError(f"trace {trace + 1} sequence fields changed")
                if i16(trace_header, 28) != expected_code:
                    raise ValueError(f"trace {trace + 1} component code changed")
                if u16(trace_header, 114) != sample_count or u16(trace_header, 116) != dt_us:
                    raise ValueError(f"trace {trace + 1} sample axis changed")
                scalel, scalco = i16(trace_header, 68), i16(trace_header, 70)
                receiver_xyz = (
                    scaled(i32(trace_header, 80), scalco),
                    scaled(i32(trace_header, 84), scalco),
                    -scaled(i32(trace_header, 40), scalel),
                )
                source_xyz = (
                    scaled(i32(trace_header, 72), scalco),
                    scaled(i32(trace_header, 76), scalco),
                    scaled(i32(trace_header, 48), scalel),
                )
                if receiver_xyz != (expected_x, expected_y, 0.0):
                    raise ValueError(f"trace {trace + 1} receiver coordinate changed")
                if source_xyz != (arguments.source_x, arguments.source_y, arguments.source_z):
                    raise ValueError(f"trace {trace + 1} source coordinate changed")

                values = struct.unpack(unpack_format, sample_bytes)
                if not all(math.isfinite(value) for value in values):
                    raise ValueError(f"trace {trace + 1} contains a non-finite sample")
                components.append(values)
                trace_peak = max(abs(value) for value in values)
                peaks[component].append(trace_peak)
                energy_parts[component].append(math.fsum(value * value for value in values))
                finite_count += sample_count
                nonzero_count += sum(value != 0.0 for value in values)
                maximum_absolute = max(maximum_absolute, trace_peak)
                for sample, value in enumerate(values):
                    envelope[sample] = max(envelope[sample], abs(value))

            receiver_peak = max(envelope)
            if receiver_peak <= 0.0:
                raise ValueError(f"receiver {receiver} has zero three-component data")
            first = next(
                sample
                for sample, value in enumerate(envelope)
                if value >= arguments.significance_fraction * receiver_peak
            )
            observed = (first + 1) * dt_s
            arrivals.append(observed)
            distance = math.dist(
                (arguments.source_x, arguments.source_y, arguments.source_z),
                (expected_x, expected_y, 0.0),
            )
            lower = distance / arguments.max_vp - dt_s
            upper = arguments.peak_delay + distance / arguments.min_vs + dt_s
            if lower <= observed <= upper:
                arrivals_in_bounds += 1

            dx = expected_x - arguments.source_x
            dy = expected_y - arguments.source_y
            if dx != 0.0 and dy != 0.0:
                dz = -arguments.source_z
                length = math.sqrt(dx * dx + dy * dy + dz * dz)
                direction = (dx / length, dy / length, dz / length)
                end = min(sample_count, first + 350)
                radial = [
                    sum(components[axis][sample] * direction[axis] for axis in range(3))
                    for sample in range(first, end)
                ]
                signed_peak = max(radial, key=abs)
                polarity_total += 1
                polarity_matches += signed_peak * dx * dy > 0.0

    energies = [math.fsum(parts) for parts in energy_parts]
    if arrivals_in_bounds != receiver_count:
        raise ValueError(
            f"only {arrivals_in_bounds}/{receiver_count} arrivals are inside "
            "the conservative travel window"
        )
    report: dict[str, object] = {
        "bytes": file_bytes,
        "receivers": receiver_count,
        "traces": trace_count,
        "samples_per_trace": sample_count,
        "dt_us": dt_us,
        "finite": finite_count,
        "nonzero": nonzero_count,
        "max_abs": maximum_absolute,
        "energies": energies,
        "arrival_min_s": min(arrivals),
        "arrival_max_s": max(arrivals),
        "arrivals_in_bounds": arrivals_in_bounds,
        "polarity_matches": polarity_matches,
        "polarity_total": polarity_total,
        "sha256": digest.hexdigest(),
    }
    return report, peaks, arrivals


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output_svg", type=Path)
    parser.add_argument(
        "--report",
        type=Path,
        help="validation report path (default: output SVG with .txt suffix)",
    )
    parser.add_argument("--side", type=int, default=101)
    parser.add_argument("--x0", type=float, default=500.0)
    parser.add_argument("--y0", type=float, default=500.0)
    parser.add_argument("--spacing", type=float, default=40.0)
    parser.add_argument("--source-x", type=float, default=2500.0)
    parser.add_argument("--source-y", type=float, default=2500.0)
    parser.add_argument("--source-z", type=float, default=1100.0)
    parser.add_argument("--max-vp", type=float, default=6000.0)
    parser.add_argument("--min-vs", type=float, default=1412.059814453125)
    parser.add_argument("--peak-delay", type=float, default=1.0 / 3.0)
    parser.add_argument("--significance-fraction", type=float, default=1.0e-4)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        if arguments.side <= 1 or arguments.spacing <= 0.0:
            raise ValueError("receiver side and spacing must be positive")
        report, peaks, arrivals = verify_and_collect(arguments)
        peak_values = [value for component in peaks for value in component]
        peak_clip = percentile(peak_values, 99.5)
        x_range = (
            arguments.x0,
            arguments.x0 + (arguments.side - 1) * arguments.spacing,
        )
        y_range = (
            arguments.y0,
            arguments.y0 + (arguments.side - 1) * arguments.spacing,
        )
        arguments.output_svg.parent.mkdir(parents=True, exist_ok=True)
        arguments.output_svg.write_text(
            maps_svg(
                arguments.input,
                arguments.side,
                peaks,
                arrivals,
                peak_clip,
                (min(arrivals), max(arrivals)),
                x_range,
                y_range,
                (arguments.source_x, arguments.source_y),
            ),
            encoding="utf-8",
        )
        report_path = (
            arguments.report
            if arguments.report is not None
            else arguments.output_svg.with_suffix(".txt")
        )
        report_path.parent.mkdir(parents=True, exist_ok=True)
        lines = [f"{key}={value}" for key, value in report.items()]
        lines.append("result=PASS")
        report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print("\n".join(lines))
        print(f"map_svg={arguments.output_svg}")
        print(f"report={report_path}")
        print("result=PASS")
        return 0
    except (OSError, ValueError, struct.error) as error:
        print(f"dense SEG-Y verification failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
