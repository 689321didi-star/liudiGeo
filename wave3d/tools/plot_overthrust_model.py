#!/usr/bin/env python3
"""Render audited original and prepared Overthrust model sections."""

from __future__ import annotations

import argparse
import base64
import html
import math
import mmap
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

sys.dont_write_bytecode = True
from plot_segy import png_bytes  # noqa: E402


SOURCE_SHAPE = (187, 801, 801)  # z, y, x
CROP = (154, 153, 0, 200, 200, 187)  # x0, y0, z0, nx, ny, nz
PREPARED_SHAPE = (187, 200, 200)
SPACING_M = 25.0
SOURCE_VP_RANGE = (2178.83447265625, 6000.0)
PROPERTY_RANGES = {
    "vp": (2445.75927734375, 6000.0, "Vp (m/s)"),
    "vs": (1412.059814453125, 3464.1015625, "Vs (m/s)"),
    "rho": (2180.043212890625, 2728.346435546875, "density (kg/m³)"),
}
VIRIDIS = (
    (0.00, (68, 1, 84)),
    (0.25, (59, 82, 139)),
    (0.50, (33, 145, 140)),
    (0.75, (94, 201, 98)),
    (1.00, (253, 231, 37)),
)


def u32_le(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def mat_element(data: bytes, offset: int) -> tuple[int, int, int, int]:
    if len(data) - offset < 8:
        raise ValueError("truncated MATLAB v5 element tag")
    first = u32_le(data, offset)
    small_bytes = first >> 16
    if small_bytes:
        if small_bytes > 4:
            raise ValueError("invalid MATLAB v5 small element")
        return first & 0xFFFF, small_bytes, offset + 4, offset + 8
    payload_bytes = u32_le(data, offset + 4)
    return first, payload_bytes, offset + 8, offset + 8 + (payload_bytes + 7) // 8 * 8


def matrix_header(prefix: bytes) -> tuple[str, tuple[int, ...], int, int, int]:
    matrix_type, matrix_bytes, matrix_data, _ = mat_element(prefix, 0)
    if matrix_type != 14:
        raise ValueError("compressed MAT element is not a matrix")
    flags_type, flags_bytes, flags_data, offset = mat_element(prefix, matrix_data)
    if flags_type != 6 or flags_bytes != 8 or prefix[flags_data] != 6:
        raise ValueError("MAT variable is not a real double array")
    dims_type, dims_bytes, dims_data, offset = mat_element(prefix, offset)
    if dims_type != 5 or dims_bytes % 4:
        raise ValueError("invalid MAT dimensions")
    dimensions = tuple(
        u32_le(prefix, dims_data + index) for index in range(0, dims_bytes, 4)
    )
    name_type, name_bytes, name_data, offset = mat_element(prefix, offset)
    if name_type != 1:
        raise ValueError("invalid MAT variable name")
    name = prefix[name_data : name_data + name_bytes].decode("ascii")
    real_type, real_bytes, real_data, _ = mat_element(prefix, offset)
    return name, dimensions, real_type, real_data, real_bytes


def find_mat_data(path: Path) -> tuple[bytes, int, int]:
    with path.open("rb") as source:
        header = source.read(128)
        if len(header) != 128 or header[126:128] != b"IM":
            raise ValueError("input is not a little-endian MATLAB v5 file")
        while tag := source.read(8):
            if len(tag) != 8:
                raise ValueError("truncated top-level MAT tag")
            element_type, compressed_bytes = struct.unpack("<II", tag)
            if element_type != 15:
                raise ValueError("expected a compressed top-level MAT element")
            compressed = source.read(compressed_bytes)
            if len(compressed) != compressed_bytes:
                raise ValueError("truncated compressed MAT element")
            decoder = zlib.decompressobj()
            prefix = decoder.decompress(compressed, 128)
            name, dimensions, real_type, real_offset, real_bytes = matrix_header(prefix)
            if name != "data":
                continue
            if dimensions != SOURCE_SHAPE or real_type != 9:
                raise ValueError("MAT data must be double [187,801,801]")
            expected_bytes = 187 * 801 * 801 * 8
            if real_bytes != expected_bytes:
                raise ValueError("MAT data payload size changed")
            return compressed, real_offset, real_bytes
    raise ValueError("MAT data variable is missing")


def inflate_mat_real(
    compressed: bytes,
    real_offset: int,
    real_bytes: int,
    output: Path,
) -> None:
    decoder = zlib.decompressobj()
    decompressed_offset = 0
    written = 0
    with output.open("wb") as target:
        for begin in range(0, len(compressed), 1 << 20):
            chunk = decoder.decompress(compressed[begin : begin + (1 << 20)])
            chunk_begin = decompressed_offset
            chunk_end = chunk_begin + len(chunk)
            copy_begin = max(chunk_begin, real_offset)
            copy_end = min(chunk_end, real_offset + real_bytes)
            if copy_begin < copy_end:
                target.write(chunk[copy_begin - chunk_begin : copy_end - chunk_begin])
                written += copy_end - copy_begin
            decompressed_offset = chunk_end
        tail = decoder.flush()
        chunk_begin = decompressed_offset
        chunk_end = chunk_begin + len(tail)
        copy_begin = max(chunk_begin, real_offset)
        copy_end = min(chunk_end, real_offset + real_bytes)
        if copy_begin < copy_end:
            target.write(tail[copy_begin - chunk_begin : copy_end - chunk_begin])
            written += copy_end - copy_begin
    if written != real_bytes:
        raise ValueError(f"decoded {written} MAT data bytes; expected {real_bytes}")


def original_views(raw_path: Path) -> dict[str, tuple[int, int, list[float]]]:
    nz, ny, nx = SOURCE_SHAPE
    x_slice = CROP[0] + CROP[3] // 2
    y_slice = CROP[1] + CROP[4] // 2
    z_slice = nz // 2
    with raw_path.open("rb") as source, mmap.mmap(
        source.fileno(), 0, access=mmap.ACCESS_READ
    ) as raw:
        xy = [0.0] * (nx * ny)
        for x in range(nx):
            x_base = nz * ny * x
            for y in range(ny):
                xy[y * nx + x] = struct.unpack_from(
                    "<d", raw, 8 * (z_slice + nz * y + x_base)
                )[0]

        xz = [0.0] * (nx * nz)
        for x in range(nx):
            offset = 8 * nz * (y_slice + ny * x)
            column = struct.unpack_from(f"<{nz}d", raw, offset)
            for z, value in enumerate(column):
                xz[z * nx + x] = value

        yz_source = struct.unpack_from(
            f"<{ny * nz}d", raw, 8 * nz * ny * x_slice
        )
        yz = [yz_source[z + nz * y] for z in range(nz) for y in range(ny)]
    return {"xy": (nx, ny, xy), "xz": (nx, nz, xz), "yz": (ny, nz, yz)}


def dump_hdf5_datasets(
    hdf5: Path,
    h5dump: Path,
    directory: Path,
) -> dict[str, Path]:
    result = {}
    expected_bytes = 187 * 200 * 200 * 4
    for name in PROPERTY_RANGES:
        output = directory / f"{name}.f32le"
        subprocess.run(
            [str(h5dump), "-d", f"/{name}", "-b", "LE", "-o", str(output), str(hdf5)],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        if output.stat().st_size != expected_bytes:
            raise ValueError(f"HDF5 /{name} raw byte count changed")
        result[name] = output
    return result


def prepared_views(raw_path: Path) -> dict[str, tuple[int, int, list[float]]]:
    nz, ny, nx = PREPARED_SHAPE
    x_slice, y_slice, z_slice = nx // 2, ny // 2, nz // 2
    with raw_path.open("rb") as source, mmap.mmap(
        source.fileno(), 0, access=mmap.ACCESS_READ
    ) as raw:
        xy_offset = 4 * z_slice * ny * nx
        xy = list(struct.unpack_from(f"<{ny * nx}f", raw, xy_offset))
        xz = []
        for z in range(nz):
            offset = 4 * ((z * ny + y_slice) * nx)
            xz.extend(struct.unpack_from(f"<{nx}f", raw, offset))
        yz = [
            struct.unpack_from("<f", raw, 4 * ((z * ny + y) * nx + x_slice))[0]
            for z in range(nz)
            for y in range(ny)
        ]
    return {"xy": (nx, ny, xy), "xz": (nx, nz, xz), "yz": (ny, nz, yz)}


def model_color(value: float, minimum: float, maximum: float) -> tuple[int, int, int]:
    fraction = max(0.0, min(1.0, (value - minimum) / (maximum - minimum)))
    for (left_x, left), (right_x, right) in zip(VIRIDIS, VIRIDIS[1:]):
        if fraction <= right_x:
            weight = (fraction - left_x) / (right_x - left_x)
            return tuple(round(a + (b - a) * weight) for a, b in zip(left, right))
    return VIRIDIS[-1][1]


def render_model(
    view: tuple[int, int, list[float]],
    limits: tuple[float, float],
) -> bytes:
    width, height, values = view
    if len(values) != width * height:
        raise ValueError("model view shape mismatch")
    minimum, maximum = limits
    pixels = bytearray(width * height * 3)
    for index, value in enumerate(values):
        if not math.isfinite(value):
            raise ValueError("model view contains a non-finite value")
        rgb = model_color(value, minimum, maximum)
        pixels[index * 3 : index * 3 + 3] = bytes(rgb)
    return png_bytes(width, height, bytes(pixels))


def require_crop_view_match(
    source: dict[str, tuple[int, int, list[float]]],
    prepared: dict[str, tuple[int, int, list[float]]],
) -> None:
    x0, y0, _, nx, ny, nz = CROP
    source_nx, source_ny = SOURCE_SHAPE[2], SOURCE_SHAPE[1]
    expected_xy = [
        source["xy"][2][(y0 + y) * source_nx + x0 + x]
        for y in range(ny)
        for x in range(nx)
    ]
    expected_xz = [
        source["xz"][2][z * source_nx + x0 + x]
        for z in range(nz)
        for x in range(nx)
    ]
    expected_yz = [
        source["yz"][2][z * source_ny + y0 + y]
        for z in range(nz)
        for y in range(ny)
    ]
    for name, expected in (("xy", expected_xy), ("xz", expected_xz), ("yz", expected_yz)):
        if expected != prepared[name][2]:
            raise ValueError(
                f"prepared HDF5 Vp {name.upper()} view differs from the MAT crop"
            )


def data_uri(png: bytes) -> str:
    return "data:image/png;base64," + base64.b64encode(png).decode("ascii")


def svg_header(width: int, height: int, title: str) -> list[str]:
    stops = "".join(
        f'<stop offset="{position * 100:g}%" stop-color="rgb{color}"/>'
        for position, color in VIRIDIS
    )
    return [
        '<?xml version="1.0" encoding="UTF-8"?>',
        (
            f'<svg xmlns="http://www.w3.org/2000/svg" '
            f'xmlns:xlink="http://www.w3.org/1999/xlink" width="{width}" '
            f'height="{height}" viewBox="0 0 {width} {height}">'
        ),
        '<rect width="100%" height="100%" fill="white"/>',
        f'<defs><linearGradient id="viridis">{stops}</linearGradient></defs>',
        (
            f'<text x="{width / 2:g}" y="31" text-anchor="middle" '
            'font-family="sans-serif" font-size="21" font-weight="600">'
            f'{html.escape(title)}</text>'
        ),
    ]


def original_svg(views: dict[str, tuple[int, int, list[float]]]) -> str:
    width, height = 1000, 1030
    parts = svg_header(width, height, "Original SEG/EAGE Overthrust Vp and selected crop")
    xy_x, xy_y, xy_size = 90, 72, 650
    xz_x, xz_y, xz_w, xz_h = 65, 815, 420, 98
    yz_x, yz_y, yz_w, yz_h = 535, 815, 420, 98
    rendered = {name: render_model(view, SOURCE_VP_RANGE) for name, view in views.items()}
    parts.extend(
        [
            f'<image x="{xy_x}" y="{xy_y}" width="{xy_size}" height="{xy_size}" xlink:href="{data_uri(rendered["xy"])}"/>',
            f'<rect x="{xy_x}" y="{xy_y}" width="{xy_size}" height="{xy_size}" fill="none" stroke="#222"/>',
            f'<text x="{xy_x + xy_size / 2:g}" y="{xy_y - 10}" text-anchor="middle" font-family="sans-serif" font-size="14">XY at z=2.325 km</text>',
            f'<text x="{xy_x + xy_size / 2:g}" y="{xy_y + xy_size + 25}" text-anchor="middle" font-family="sans-serif" font-size="12">x: 0–20 km</text>',
            f'<text x="{xy_x - 48}" y="{xy_y + xy_size / 2:g}" transform="rotate(-90 {xy_x - 48} {xy_y + xy_size / 2:g})" text-anchor="middle" font-family="sans-serif" font-size="12">y: 0–20 km</text>',
            f'<image x="{xz_x}" y="{xz_y}" width="{xz_w}" height="{xz_h}" xlink:href="{data_uri(rendered["xz"])}"/>',
            f'<rect x="{xz_x}" y="{xz_y}" width="{xz_w}" height="{xz_h}" fill="none" stroke="#222"/>',
            f'<text x="{xz_x + xz_w / 2:g}" y="{xz_y - 10}" text-anchor="middle" font-family="sans-serif" font-size="13">XZ at y=6.325 km</text>',
            f'<text x="{xz_x + xz_w / 2:g}" y="{xz_y + xz_h + 18}" text-anchor="middle" font-family="sans-serif" font-size="11">x: 0–20 km; z: 0–4.65 km</text>',
            f'<image x="{yz_x}" y="{yz_y}" width="{yz_w}" height="{yz_h}" xlink:href="{data_uri(rendered["yz"])}"/>',
            f'<rect x="{yz_x}" y="{yz_y}" width="{yz_w}" height="{yz_h}" fill="none" stroke="#222"/>',
            f'<text x="{yz_x + yz_w / 2:g}" y="{yz_y - 10}" text-anchor="middle" font-family="sans-serif" font-size="13">YZ at x=6.350 km</text>',
            f'<text x="{yz_x + yz_w / 2:g}" y="{yz_y + yz_h + 18}" text-anchor="middle" font-family="sans-serif" font-size="11">y: 0–20 km; z: 0–4.65 km</text>',
        ]
    )
    crop_x = xy_x + CROP[0] / SOURCE_SHAPE[2] * xy_size
    crop_y = xy_y + CROP[1] / SOURCE_SHAPE[1] * xy_size
    crop_w = CROP[3] / SOURCE_SHAPE[2] * xy_size
    crop_h = CROP[4] / SOURCE_SHAPE[1] * xy_size
    slice_x = xy_x + (CROP[0] + CROP[3] / 2) / SOURCE_SHAPE[2] * xy_size
    slice_y = xy_y + (CROP[1] + CROP[4] / 2) / SOURCE_SHAPE[1] * xy_size
    parts.extend(
        [
            f'<rect x="{crop_x:g}" y="{crop_y:g}" width="{crop_w:g}" height="{crop_h:g}" fill="#ff1744" fill-opacity="0.10" stroke="#ff1744" stroke-width="3"/>',
            f'<line x1="{slice_x:g}" y1="{xy_y}" x2="{slice_x:g}" y2="{xy_y + xy_size}" stroke="#00bcd4" stroke-width="1" stroke-dasharray="5 4"/>',
            f'<line x1="{xy_x}" y1="{slice_y:g}" x2="{xy_x + xy_size}" y2="{slice_y:g}" stroke="#00bcd4" stroke-width="1" stroke-dasharray="5 4"/>',
            f'<circle cx="{slice_x:g}" cy="{slice_y:g}" r="6" fill="none" stroke="white" stroke-width="2"/>',
            f'<rect x="{xz_x + CROP[0] / SOURCE_SHAPE[2] * xz_w:g}" y="{xz_y}" width="{CROP[3] / SOURCE_SHAPE[2] * xz_w:g}" height="{xz_h}" fill="#ff1744" fill-opacity="0.10" stroke="#ff1744" stroke-width="2"/>',
            f'<rect x="{yz_x + CROP[1] / SOURCE_SHAPE[1] * yz_w:g}" y="{yz_y}" width="{CROP[4] / SOURCE_SHAPE[1] * yz_w:g}" height="{yz_h}" fill="#ff1744" fill-opacity="0.10" stroke="#ff1744" stroke-width="2"/>',
            f'<circle cx="{xz_x + (CROP[0] + 100) / SOURCE_SHAPE[2] * xz_w:g}" cy="{xz_y + 44 / SOURCE_SHAPE[0] * xz_h:g}" r="4" fill="#ff1744" stroke="white" stroke-width="1"/>',
            f'<circle cx="{yz_x + (CROP[1] + 100) / SOURCE_SHAPE[1] * yz_w:g}" cy="{yz_y + 44 / SOURCE_SHAPE[0] * yz_h:g}" r="4" fill="#ff1744" stroke="white" stroke-width="1"/>',
            '<text x="770" y="113" font-family="sans-serif" font-size="13" font-weight="600" fill="#d50000">selected crop</text>',
            '<text x="770" y="134" font-family="sans-serif" font-size="12">x index [154,354); samples 3.850–8.825 km</text>',
            '<text x="770" y="152" font-family="sans-serif" font-size="12">y index [153,353); samples 3.825–8.800 km</text>',
            '<text x="770" y="170" font-family="sans-serif" font-size="12">z index [0,187); samples 0–4.650 km</text>',
            '<text x="770" y="200" font-family="sans-serif" font-size="12" fill="#00838f">cyan: section planes</text>',
            '<text x="770" y="218" font-family="sans-serif" font-size="12">circle: source projection</text>',
            '<rect x="790" y="270" width="25" height="400" fill="url(#viridis)" stroke="#333"/>',
            f'<text x="825" y="281" font-family="sans-serif" font-size="11">{SOURCE_VP_RANGE[1]:g}</text>',
            f'<text x="825" y="668" font-family="sans-serif" font-size="11">{SOURCE_VP_RANGE[0]:.3f}</text>',
            '<text x="875" y="470" transform="rotate(-90 875 470)" text-anchor="middle" font-family="sans-serif" font-size="12">Vp (m/s)</text>',
        ]
    )
    parts.append('</svg>')
    return "\n".join(parts) + "\n"


def prepared_svg(
    views: dict[str, tuple[int, int, list[float]]],
    title: str,
    limits: tuple[float, float],
    property_label: str,
) -> str:
    width, height = 1320, 540
    parts = svg_header(width, height, title)
    panel_x = (50, 475, 900)
    names = ("xy", "xz", "yz")
    labels = ("XY at z=2.325 km", "XZ at y=2.500 km", "YZ at x=2.500 km")
    display = ((360, 360), (360, 337), (360, 337))
    rendered = {name: render_model(views[name], limits) for name in names}
    for index, (name, label, (panel_w, panel_h)) in enumerate(zip(names, labels, display)):
        x, y = panel_x[index], 82
        axis_text = (
            "x/y: 0–4.975 km"
            if name == "xy"
            else "horizontal 0–4.975 km; depth 0–4.650 km"
        )
        parts.extend(
            [
                f'<text x="{x + panel_w / 2:g}" y="66" text-anchor="middle" font-family="sans-serif" font-size="14" font-weight="600">{label}</text>',
                f'<image x="{x}" y="{y}" width="{panel_w}" height="{panel_h}" xlink:href="{data_uri(rendered[name])}"/>',
                f'<rect x="{x}" y="{y}" width="{panel_w}" height="{panel_h}" fill="none" stroke="#222"/>',
            ]
        )
        if name == "xy":
            source_x, source_y = x + panel_w / 2, y + panel_h / 2
            aperture_start = x + 20 / 200 * panel_w
            aperture_size = 160 / 200 * panel_w
            parts.append(f'<rect x="{aperture_start:g}" y="{y + 20 / 200 * panel_h:g}" width="{aperture_size:g}" height="{aperture_size:g}" fill="none" stroke="white" stroke-width="1.5" stroke-dasharray="5 3"/>')
        else:
            source_x = x + panel_w / 2
            source_y = y + 44 / 187 * panel_h
            aperture_start = x + 20 / 200 * panel_w
            aperture_size = 160 / 200 * panel_w
            parts.append(f'<line x1="{aperture_start:g}" y1="{y + 1}" x2="{aperture_start + aperture_size:g}" y2="{y + 1}" stroke="white" stroke-width="3"/>')
        parts.extend(
            [
                f'<circle cx="{source_x:g}" cy="{source_y:g}" r="5" fill="#ff1744" stroke="white" stroke-width="1.5"/>',
                f'<text x="{x + panel_w / 2:g}" y="{y + panel_h + 22}" text-anchor="middle" font-family="sans-serif" font-size="11">{axis_text}</text>',
            ]
        )
    parts.extend(
        [
            '<rect x="470" y="475" width="380" height="18" fill="url(#viridis)" stroke="#333"/>',
            f'<text x="470" y="511" font-family="sans-serif" font-size="11">{limits[0]:.3f}</text>',
            f'<text x="850" y="511" text-anchor="end" font-family="sans-serif" font-size="11">{limits[1]:.3f}</text>',
            f'<text x="660" y="511" text-anchor="middle" font-family="sans-serif" font-size="12">{html.escape(property_label)}</text>',
            '<text x="50" y="508" font-family="sans-serif" font-size="11" fill="#d50000">red circle: Mxy source</text>',
            '<text x="1010" y="508" font-family="sans-serif" font-size="11">white: 101×101 receiver aperture</text>',
        ]
    )
    parts.append('</svg>')
    return "\n".join(parts) + "\n"


def properties_svg(
    property_views: dict[str, dict[str, tuple[int, int, list[float]]]],
) -> str:
    width, height = 1320, 545
    parts = svg_header(width, height, "Prepared forward-model properties — XZ section at y=2.500 km")
    for index, name in enumerate(("vp", "vs", "rho")):
        minimum, maximum, label = PROPERTY_RANGES[name]
        x, y, panel_w, panel_h = 50 + index * 425, 82, 360, 337
        image = render_model(property_views[name]["xz"], (minimum, maximum))
        parts.extend(
            [
                f'<text x="{x + panel_w / 2:g}" y="66" text-anchor="middle" font-family="sans-serif" font-size="14" font-weight="600">{html.escape(label)}</text>',
                f'<image x="{x}" y="{y}" width="{panel_w}" height="{panel_h}" xlink:href="{data_uri(image)}"/>',
                f'<rect x="{x}" y="{y}" width="{panel_w}" height="{panel_h}" fill="none" stroke="#222"/>',
                f'<circle cx="{x + panel_w / 2:g}" cy="{y + 44 / 187 * panel_h:g}" r="5" fill="#ff1744" stroke="white" stroke-width="1.5"/>',
                f'<rect x="{x}" y="458" width="{panel_w}" height="16" fill="url(#viridis)" stroke="#333"/>',
                f'<text x="{x}" y="493" font-family="sans-serif" font-size="11">{minimum:.3f}</text>',
                f'<text x="{x + panel_w}" y="493" text-anchor="end" font-family="sans-serif" font-size="11">{maximum:.3f}</text>',
                f'<text x="{x + panel_w / 2:g}" y="515" text-anchor="middle" font-family="sans-serif" font-size="11">x: 0–4.975 km; z: 0–4.650 km</text>',
            ]
        )
    parts.append('</svg>')
    return "\n".join(parts) + "\n"


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mat", type=Path, help="audited overthrust_3d_vp.mat")
    parser.add_argument("hdf5", type=Path, help="prepared Overthrust HDF5 model")
    parser.add_argument("output_directory", type=Path)
    parser.add_argument(
        "--h5dump",
        type=Path,
        default=Path(shutil.which("h5dump") or "h5dump"),
        help="path to the HDF5 h5dump executable",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        arguments.output_directory.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="wave3d-overthrust-plots-") as temporary:
            temporary_path = Path(temporary)
            compressed, real_offset, real_bytes = find_mat_data(arguments.mat)
            source_raw = temporary_path / "source_vp.f64le"
            inflate_mat_real(compressed, real_offset, real_bytes, source_raw)
            source_views = original_views(source_raw)

            datasets = dump_hdf5_datasets(
                arguments.hdf5, arguments.h5dump, temporary_path
            )
            properties = {
                name: prepared_views(path) for name, path in datasets.items()
            }
            require_crop_view_match(source_views, properties["vp"])
            print("crop_slice_match=PASS")

            outputs = {
                "original_vp_three_views_with_crop.svg": original_svg(source_views),
                "prepared_vp_three_views.svg": prepared_svg(
                    properties["vp"],
                    "Prepared forward Vp — cropped 200×200×187 model",
                    PROPERTY_RANGES["vp"][:2],
                    PROPERTY_RANGES["vp"][2],
                ),
                "prepared_model_properties_xz.svg": properties_svg(properties),
            }
            for name, document in outputs.items():
                path = arguments.output_directory / name
                path.write_text(document, encoding="utf-8")
                print(f"output={path}")
        return 0
    except (OSError, ValueError, UnicodeError, struct.error, subprocess.CalledProcessError, zlib.error) as error:
        print(f"Overthrust model plotting failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
