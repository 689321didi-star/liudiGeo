#!/usr/bin/env python3
"""Render a Wave3D three-component SEG-Y record without third-party packages."""

from __future__ import annotations

import argparse
import base64
from dataclasses import dataclass
import html
import math
from pathlib import Path
import struct
import sys
import zlib


COMPONENTS = (
    (14, "VX / east"),
    (13, "VY / north"),
    (12, "VZ / down"),
)


@dataclass(frozen=True)
class SegyRecord:
    sample_count: int
    dt_s: float
    traces: dict[int, list[tuple[float, ...]]]
    total_receiver_count: int
    selected_receiver_indices: tuple[int, ...]
    selection_label: str


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from(">H", data, offset)[0]


def i16(data: bytes, offset: int) -> int:
    return struct.unpack_from(">h", data, offset)[0]


def read_wave3d_segy(
    path: Path,
    receiver_row: int | None = None,
    receiver_column: int | None = None,
) -> SegyRecord:
    file_bytes = path.stat().st_size
    if file_bytes < 3600:
        raise ValueError("file is shorter than the SEG-Y headers")
    with path.open("rb") as source:
        file_header = source.read(3600)
    if len(file_header) != 3600:
        raise ValueError("cannot read the SEG-Y headers")
    sample_count = u16(file_header, 3220)
    dt_us = u16(file_header, 3216)
    format_code = u16(file_header, 3224)
    extended_headers = i16(file_header, 3504)
    if sample_count == 0 or dt_us == 0:
        raise ValueError("SEG-Y sample count and interval must be positive")
    if format_code != 5:
        raise ValueError(
            f"only big-endian IEEE float32 format code 5 is supported; found {format_code}"
        )
    if extended_headers != 0:
        raise ValueError(
            f"extended textual headers are not supported; found {extended_headers}"
        )

    trace_bytes = 240 + 4 * sample_count
    payload_bytes = file_bytes - 3600
    if payload_bytes % trace_bytes != 0:
        raise ValueError("file size is inconsistent with fixed-length traces")
    trace_count = payload_bytes // trace_bytes
    if trace_count % len(COMPONENTS) != 0:
        raise ValueError("trace count is not divisible into VX/VY/VZ triplets")
    receiver_count = trace_count // len(COMPONENTS)
    receiver_side = math.isqrt(receiver_count)
    square_receiver_grid = receiver_side * receiver_side == receiver_count
    if receiver_row is not None or receiver_column is not None:
        if not square_receiver_grid:
            raise ValueError("row/column selection requires a square receiver grid")
        selected_axis = receiver_row if receiver_row is not None else receiver_column
        if selected_axis is None or not 0 <= selected_axis < receiver_side:
            raise ValueError(
                f"receiver row/column must be in [0,{receiver_side - 1}]"
            )
        if receiver_row is not None:
            selected_receivers = tuple(
                receiver_row * receiver_side + x for x in range(receiver_side)
            )
            selection_label = (
                f"x-index 0–{receiver_side - 1} at y-index {receiver_row}"
            )
        else:
            selected_receivers = tuple(
                y * receiver_side + receiver_column for y in range(receiver_side)
            )
            selection_label = (
                f"y-index 0–{receiver_side - 1} at x-index {receiver_column}"
            )
    elif receiver_count <= 500:
        selected_receivers = tuple(range(receiver_count))
        selection_label = "receiver index"
    else:
        raise ValueError(
            f"record contains {receiver_count} receivers; select a square-grid "
            "line with --receiver-row or --receiver-column"
        )

    grouped: dict[int, list[tuple[float, ...]]] = {
        code: [] for code, _ in COMPONENTS
    }
    unpack_format = f">{sample_count}f"
    with path.open("rb") as source:
        for receiver in selected_receivers:
            for component, (expected_code, _) in enumerate(COMPONENTS):
                trace = receiver * len(COMPONENTS) + component
                offset = 3600 + trace * trace_bytes
                source.seek(offset)
                trace_header = source.read(240)
                sample_bytes = source.read(4 * sample_count)
                if len(trace_header) != 240 or len(sample_bytes) != 4 * sample_count:
                    raise ValueError(f"trace {trace + 1} is truncated")
                code = i16(trace_header, 28)
                if code != expected_code:
                    raise ValueError(
                        f"trace {trace + 1} has component code {code}; "
                        f"expected receiver-major VX/VY/VZ code {expected_code}"
                    )
                trace_sample_count = u16(trace_header, 114)
                trace_dt_us = u16(trace_header, 116)
                if trace_sample_count != sample_count or trace_dt_us != dt_us:
                    raise ValueError(
                        f"trace {trace + 1} sample axis differs from the binary header"
                    )
                values = struct.unpack(unpack_format, sample_bytes)
                if not all(math.isfinite(value) for value in values):
                    raise ValueError(f"trace {trace + 1} contains a non-finite sample")
                grouped[code].append(values)

    counts = {len(grouped[code]) for code, _ in COMPONENTS}
    if len(counts) != 1 or counts == {0}:
        raise ValueError("VX, VY, and VZ trace counts are unequal or empty")
    return SegyRecord(
        sample_count,
        dt_us * 1.0e-6,
        grouped,
        receiver_count,
        selected_receivers,
        selection_label,
    )


def percentile(values: list[float], percent: float) -> float:
    values.sort()
    index = int(round((len(values) - 1) * percent / 100.0))
    return values[index]


def component_clip(traces: list[tuple[float, ...]], percent: float) -> float:
    absolute = [abs(value) for trace in traces for value in trace]
    clip = percentile(absolute, percent)
    if not math.isfinite(clip) or clip <= 0.0:
        clip = max(absolute, default=0.0)
    if clip <= 0.0:
        raise ValueError("component contains no nonzero samples")
    return clip


def color(value: float) -> tuple[int, int, int]:
    value = max(-1.0, min(1.0, value))
    if value < 0.0:
        weight = -value
        return (
            round(247 + (33 - 247) * weight),
            round(247 + (102 - 247) * weight),
            round(247 + (172 - 247) * weight),
        )
    weight = value
    return (
        round(247 + (178 - 247) * weight),
        round(247 + (24 - 247) * weight),
        round(247 + (43 - 247) * weight),
    )


def render_panel(
    traces: list[tuple[float, ...]],
    clip: float,
    height: int,
    trace_pixels: int,
) -> tuple[int, int, bytes]:
    sample_count = len(traces[0])
    width = len(traces) * trace_pixels
    pixels = bytearray(width * height * 3)
    for y in range(height):
        sample_begin = y * sample_count // height
        sample_end = max(sample_begin + 1, (y + 1) * sample_count // height)
        for trace_index, trace in enumerate(traces):
            segment = trace[sample_begin:sample_end]
            value = max(segment, key=abs) / clip
            rgb = color(value)
            start = (y * width + trace_index * trace_pixels) * 3
            for pixel in range(trace_pixels):
                offset = start + pixel * 3
                pixels[offset : offset + 3] = bytes(rgb)
    receiver_side = math.isqrt(len(traces))
    if receiver_side * receiver_side == len(traces):
        for block in range(1, receiver_side):
            x = block * receiver_side * trace_pixels
            for y in range(height):
                offset = (y * width + x) * 3
                pixels[offset : offset + 3] = b"\xd2\xd2\xd2"
    return width, height, bytes(pixels)


def png_bytes(width: int, height: int, pixels: bytes) -> bytes:
    def chunk(name: bytes, payload: bytes) -> bytes:
        body = name + payload
        return (
            struct.pack(">I", len(payload))
            + body
            + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
        )

    stride = width * 3
    scanlines = b"".join(
        b"\x00" + pixels[row * stride : (row + 1) * stride]
        for row in range(height)
    )
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(
            b"IHDR",
            struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0),
        )
        + chunk(b"IDAT", zlib.compress(scanlines, 9))
        + chunk(b"IEND", b"")
    )


def combined_png(panels: list[tuple[int, int, bytes]]) -> bytes:
    gap = 8
    height = panels[0][1]
    width = sum(panel[0] for panel in panels) + gap * (len(panels) - 1)
    pixels = bytearray([255]) * (width * height * 3)
    x_offset = 0
    for panel_width, panel_height, panel_pixels in panels:
        if panel_height != height:
            raise ValueError("component panel heights differ")
        row_bytes = panel_width * 3
        for y in range(height):
            source = y * row_bytes
            target = (y * width + x_offset) * 3
            pixels[target : target + row_bytes] = panel_pixels[
                source : source + row_bytes
            ]
        x_offset += panel_width + gap
    return png_bytes(width, height, bytes(pixels))


def svg_document(
    input_path: Path,
    record: SegyRecord,
    panels: list[tuple[int, int, bytes]],
    clips: list[float],
    clip_percent: float,
    shared_scale: bool,
) -> str:
    margin_left = 82
    margin_top = 78
    panel_gap = 42
    panel_width, panel_height, _ = panels[0]
    plot_width = 3 * panel_width + 2 * panel_gap
    canvas_width = margin_left + plot_width + 34
    canvas_height = margin_top + panel_height + 94
    trace_count = len(record.traces[COMPONENTS[0][0]])
    receiver_side = math.isqrt(trace_count)
    square_receiver_grid = (
        trace_count == record.total_receiver_count
        and receiver_side * receiver_side == trace_count
    )
    end_time = record.sample_count * record.dt_s

    parts = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        (
            f'<svg xmlns="http://www.w3.org/2000/svg" '
            f'xmlns:xlink="http://www.w3.org/1999/xlink" '
            f'width="{canvas_width}" height="{canvas_height}" '
            f'viewBox="0 0 {canvas_width} {canvas_height}">'
        ),
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        (
            f'<text x="{canvas_width / 2:.1f}" y="29" text-anchor="middle" '
            'font-family="sans-serif" font-size="20" font-weight="600">'
            'Wave3D SEG-Y three-component receiver gathers</text>'
        ),
        (
            f'<text x="{canvas_width / 2:.1f}" y="50" text-anchor="middle" '
            'font-family="sans-serif" font-size="12" fill="#555">'
            f'{html.escape(input_path.name)} · {trace_count} of '
            f'{record.total_receiver_count} receivers · '
            f'{record.sample_count} samples · dt={record.dt_s * 1000.0:g} ms'
            '</text>'
        ),
    ]
    for tick in range(5):
        y = margin_top + tick * panel_height / 4
        time_s = (record.dt_s + tick * (end_time - record.dt_s) / 4)
        parts.extend(
            [
                (
                    f'<line x1="{margin_left - 6}" y1="{y:.1f}" '
                    f'x2="{margin_left + plot_width}" y2="{y:.1f}" '
                    'stroke="#999" stroke-opacity="0.22"/>'
                ),
                (
                    f'<text x="{margin_left - 11}" y="{y + 4:.1f}" '
                    'text-anchor="end" font-family="sans-serif" '
                    f'font-size="11">{time_s:.3f}</text>'
                ),
            ]
        )
    parts.append(
        (
            f'<text x="18" y="{margin_top + panel_height / 2:.1f}" '
            f'transform="rotate(-90 18 {margin_top + panel_height / 2:.1f})" '
            'text-anchor="middle" font-family="sans-serif" font-size="13">'
            'time (s)</text>'
        )
    )

    for index, ((code, label), panel, clip) in enumerate(
        zip(COMPONENTS, panels, clips)
    ):
        x = margin_left + index * (panel_width + panel_gap)
        encoded = base64.b64encode(png_bytes(*panel)).decode("ascii")
        parts.extend(
            [
                (
                    f'<text x="{x + panel_width / 2:.1f}" y="{margin_top - 10}" '
                    'text-anchor="middle" font-family="sans-serif" '
                    f'font-size="14" font-weight="600">{label} (code {code})</text>'
                ),
                (
                    f'<image x="{x}" y="{margin_top}" width="{panel_width}" '
                    f'height="{panel_height}" preserveAspectRatio="none" '
                    f'xlink:href="data:image/png;base64,{encoded}"/>'
                ),
                (
                    f'<rect x="{x}" y="{margin_top}" width="{panel_width}" '
                    f'height="{panel_height}" fill="none" stroke="#333"/>'
                ),
                (
                    f'<text x="{x}" y="{margin_top + panel_height + 18}" '
                    'text-anchor="start" font-family="sans-serif" '
                    'font-size="10">1</text>'
                ),
                (
                    f'<text x="{x + panel_width}" y="{margin_top + panel_height + 18}" '
                    'text-anchor="end" font-family="sans-serif" '
                    f'font-size="10">{trace_count}</text>'
                ),
                (
                    f'<text x="{x + panel_width / 2:.1f}" '
                    f'y="{margin_top + panel_height + 18}" text-anchor="middle" '
                    f'font-family="sans-serif" font-size="10">'
                    + (
                        f'receiver index ({receiver_side} x positions per y row)'
                        if square_receiver_grid
                        else record.selection_label
                    )
                    + '</text>'
                ),
                (
                    f'<text x="{x + panel_width / 2:.1f}" '
                    f'y="{margin_top + panel_height + 39}" text-anchor="middle" '
                    'font-family="monospace" font-size="10" fill="#444">'
                    f'clip ±{clip:.4e} m/s</text>'
                ),
            ]
        )
    parts.append(
        (
            f'<text x="{canvas_width / 2:.1f}" y="{canvas_height - 18}" '
            'text-anchor="middle" font-family="sans-serif" font-size="11" '
            'fill="#555">blue = negative · white = zero · red = positive · '
            + (
                f'all components share the P{clip_percent:g} absolute-amplitude scale'
                if shared_scale
                else f'each component clipped independently at P{clip_percent:g}'
            )
            + '</text>'
        )
    )
    parts.append("</svg>")
    return "\n".join(parts) + "\n"


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Render Wave3D VX/VY/VZ receiver gathers from a SEG-Y Revision 1 "
            "IEEE-float record. SVG includes labels; PNG contains the three "
            "panels in VX, VY, VZ order."
        )
    )
    parser.add_argument("input", type=Path, help="input record.sgy")
    parser.add_argument("output", type=Path, help="output .svg or .png")
    parser.add_argument(
        "--height",
        type=int,
        default=900,
        help="raster panel height in pixels (default: 900)",
    )
    parser.add_argument(
        "--trace-pixels",
        type=int,
        default=3,
        help="horizontal pixels per receiver (default: 3)",
    )
    parser.add_argument(
        "--clip-percentile",
        type=float,
        default=99.5,
        help="per-component absolute-amplitude clipping percentile (default: 99.5)",
    )
    parser.add_argument(
        "--shared-scale",
        action="store_true",
        help="use one absolute-amplitude color scale for all three components",
    )
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument(
        "--receiver-row",
        type=int,
        help="plot one zero-based y row from a square receiver grid",
    )
    selection.add_argument(
        "--receiver-column",
        type=int,
        help="plot one zero-based x column from a square receiver grid",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        if arguments.height < 100 or arguments.height > 5000:
            raise ValueError("--height must be in [100,5000]")
        if arguments.trace_pixels < 1 or arguments.trace_pixels > 20:
            raise ValueError("--trace-pixels must be in [1,20]")
        if not 50.0 <= arguments.clip_percentile <= 100.0:
            raise ValueError("--clip-percentile must be in [50,100]")
        suffix = arguments.output.suffix.lower()
        if suffix not in {".svg", ".png"}:
            raise ValueError("output extension must be .svg or .png")

        record = read_wave3d_segy(
            arguments.input,
            arguments.receiver_row,
            arguments.receiver_column,
        )
        if arguments.shared_scale:
            shared_clip = component_clip(
                [
                    trace
                    for code, _ in COMPONENTS
                    for trace in record.traces[code]
                ],
                arguments.clip_percentile,
            )
            clips = [shared_clip] * len(COMPONENTS)
        else:
            clips = [
                component_clip(record.traces[code], arguments.clip_percentile)
                for code, _ in COMPONENTS
            ]
        panels = [
            render_panel(
                record.traces[code],
                clip,
                arguments.height,
                arguments.trace_pixels,
            )
            for (code, _), clip in zip(COMPONENTS, clips)
        ]
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        if suffix == ".svg":
            arguments.output.write_text(
                svg_document(
                    arguments.input,
                    record,
                    panels,
                    clips,
                    arguments.clip_percentile,
                    arguments.shared_scale,
                ),
                encoding="utf-8",
            )
        else:
            arguments.output.write_bytes(combined_png(panels))

        receiver_count = len(record.traces[COMPONENTS[0][0]])
        print(
            f"input={arguments.input} receivers={receiver_count}/"
            f"{record.total_receiver_count} selection={record.selection_label!r} "
            f"samples={record.sample_count} dt_s={record.dt_s:g}"
        )
        print(
            "clips_m_s="
            + ",".join(
                f"{label.split()[0]}:{clip:.12g}"
                for (_, label), clip in zip(COMPONENTS, clips)
            )
        )
        print(f"output={arguments.output}")
        return 0
    except (OSError, ValueError, struct.error, zlib.error) as error:
        print(f"SEG-Y plotting failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
