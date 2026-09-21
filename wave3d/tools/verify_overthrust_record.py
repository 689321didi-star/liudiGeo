#!/usr/bin/env python3
"""Independently verify the fixed production Overthrust SEG-Y triplet."""

from __future__ import annotations

import hashlib
import math
from pathlib import Path
import struct
import sys


RECEIVER_SIDE = 11
RECEIVER_COUNT = RECEIVER_SIDE * RECEIVER_SIDE
SAMPLE_COUNT = 3000
DT_US = 1000
COMPONENTS = (
    ("vx", 13, "record_vx.sgy"),
    ("vy", 14, "record_vy.sgy"),
    ("vz", 12, "record_vz.sgy"),
)
SIGNIFICANCE_FRACTION = 1.0e-4
SOURCE = (2500.0, 2500.0, 1100.0)
MAX_VP_M_S = 6000.0
MIN_VS_M_S = 1412.059814453125
RICKER_PEAK_DELAY_S = 1.0 / 3.0


def i16(data: bytes, offset: int) -> int:
    return struct.unpack_from(">h", data, offset)[0]


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from(">H", data, offset)[0]


def i32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">i", data, offset)[0]


def scaled_coordinate(stored: int, scalar: int) -> float:
    if scalar == 0:
        raise ValueError("SEG-Y coordinate/elevation scalar is zero")
    return stored / abs(scalar) if scalar < 0 else stored * scalar


def expected_receiver(receiver: int) -> tuple[float, float, float]:
    return (
        500.0 + 400.0 * (receiver % RECEIVER_SIDE),
        500.0 + 400.0 * (receiver // RECEIVER_SIDE),
        0.0,
    )


def verify_file(
    path: Path,
    component_name: str,
    component_code: int,
) -> tuple[list[tuple[float, ...]], int, int, float, float, str]:
    data = path.read_bytes()
    if len(data) < 3600:
        raise ValueError(f"{path.name} is shorter than its SEG-Y headers")
    text = data[:3200].decode("ascii")
    required_text = (
        "SEG-Y REVISION 1, BIG-ENDIAN",
        f"ONE COMPONENT PER FILE; COMPONENT={component_name.upper()}",
        "TRACE ORDER=RECEIVER",
        "NORMALIZATION NONE",
        "TIME AXIS IS (N+1)*DT",
    )
    if not all(item in text for item in required_text):
        raise ValueError(f"{path.name} textual header lost required interpretation")

    binary_checks = {
        "job identification": (i32(data, 3200), 1),
        "traces per ensemble": (i16(data, 3212), RECEIVER_COUNT),
        "sample interval": (u16(data, 3216), DT_US),
        "sample count": (u16(data, 3220), SAMPLE_COUNT),
        "format code": (u16(data, 3224), 5),
        "sorting code": (i16(data, 3228), 5),
        "measurement system": (i16(data, 3254), 1),
        "revision": (u16(data, 3500), 0x0100),
        "fixed trace flag": (i16(data, 3502), 1),
        "extended header count": (i16(data, 3504), 0),
    }
    for name, (actual, expected) in binary_checks.items():
        if actual != expected:
            raise ValueError(f"{path.name} {name} is {actual}, expected {expected}")

    trace_bytes = 240 + 4 * SAMPLE_COUNT
    expected_bytes = 3600 + RECEIVER_COUNT * trace_bytes
    if len(data) != expected_bytes:
        raise ValueError(f"{path.name} has {len(data)} bytes, expected {expected_bytes}")

    traces: list[tuple[float, ...]] = []
    nonzero_count = 0
    maximum_absolute = 0.0
    energy_terms: list[float] = []
    for receiver in range(RECEIVER_COUNT):
        offset = 3600 + receiver * trace_bytes
        header = data[offset : offset + 240]
        sequence = receiver + 1
        if tuple(i32(header, field) for field in (0, 4, 12, 24)) != (sequence,) * 4:
            raise ValueError(f"{path.name} trace {sequence} sequence fields differ")
        if i32(header, 8) != 1 or i32(header, 16) != 1 or i32(header, 20) != 1:
            raise ValueError(f"{path.name} trace {sequence} ensemble fields differ")
        if i16(header, 28) != component_code:
            raise ValueError(f"{path.name} trace {sequence} component code is incorrect")
        if i16(header, 68) != -1000 or i16(header, 70) != -1000:
            raise ValueError(f"{path.name} trace {sequence} coordinate scalars differ")
        source = (
            scaled_coordinate(i32(header, 72), i16(header, 70)),
            scaled_coordinate(i32(header, 76), i16(header, 70)),
            scaled_coordinate(i32(header, 48), i16(header, 68)),
        )
        receiver_location = (
            scaled_coordinate(i32(header, 80), i16(header, 70)),
            scaled_coordinate(i32(header, 84), i16(header, 70)),
            -scaled_coordinate(i32(header, 40), i16(header, 68)),
        )
        if source != SOURCE or receiver_location != expected_receiver(receiver):
            raise ValueError(f"{path.name} trace {sequence} geometry changed")
        if i16(header, 88) != 1:
            raise ValueError(f"{path.name} trace {sequence} coordinate units are not metres")
        if u16(header, 114) != SAMPLE_COUNT or u16(header, 116) != DT_US:
            raise ValueError(f"{path.name} trace {sequence} sample axis changed")
        values = struct.unpack_from(f">{SAMPLE_COUNT}f", data, offset + 240)
        if not all(math.isfinite(value) for value in values):
            raise ValueError(f"{path.name} trace {sequence} has non-finite samples")
        traces.append(values)
        nonzero_count += sum(value != 0.0 for value in values)
        maximum_absolute = max(maximum_absolute, max(abs(value) for value in values))
        energy_terms.extend(value * value for value in values)

    return (
        traces,
        len(data),
        nonzero_count,
        maximum_absolute,
        math.fsum(energy_terms),
        hashlib.sha256(data).hexdigest(),
    )


def verify(output_directory: Path) -> None:
    expected_entries = sorted(filename for _, _, filename in COMPONENTS)
    entries = sorted(path.name for path in output_directory.iterdir())
    if entries != expected_entries:
        raise ValueError(
            f"output directory must contain only {expected_entries}, found {entries}"
        )

    component_traces: list[list[tuple[float, ...]]] = []
    total_bytes = 0
    total_nonzero = 0
    maximum_absolute = 0.0
    energies: list[float] = []
    digests: list[tuple[str, str]] = []
    for name, code, filename in COMPONENTS:
        traces, byte_count, nonzero, maximum, energy, digest = verify_file(
            output_directory / filename, name, code
        )
        component_traces.append(traces)
        total_bytes += byte_count
        total_nonzero += nonzero
        maximum_absolute = max(maximum_absolute, maximum)
        energies.append(energy)
        digests.append((name, digest))

    first_times: list[float] = []
    lower_bounds: list[float] = []
    upper_bounds: list[float] = []
    dt_s = DT_US * 1.0e-6
    for receiver in range(RECEIVER_COUNT):
        envelope = [
            max(abs(component_traces[axis][receiver][sample]) for axis in range(3))
            for sample in range(SAMPLE_COUNT)
        ]
        peak = max(envelope)
        if peak == 0.0:
            raise ValueError(f"receiver {receiver} has zero three-component data")
        first_sample = next(
            sample for sample, value in enumerate(envelope)
            if value >= SIGNIFICANCE_FRACTION * peak
        )
        observed = (first_sample + 1) * dt_s
        distance = math.dist(SOURCE, expected_receiver(receiver))
        lower = distance / MAX_VP_M_S - dt_s
        upper = RICKER_PEAK_DELAY_S + distance / MIN_VS_M_S + dt_s
        if not lower <= observed <= upper:
            raise ValueError(
                f"receiver {receiver} first significant sample {observed} s "
                f"is outside [{lower},{upper}] s"
            )
        first_times.append(observed)
        lower_bounds.append(lower)
        upper_bounds.append(upper)

    if total_nonzero == 0 or not all(energy > 0.0 for energy in energies):
        raise ValueError("record triplet has zero total or component energy")
    print(
        f"bytes_total={total_bytes} files=3 traces_per_file={RECEIVER_COUNT} "
        f"samples_per_trace={SAMPLE_COUNT} dt_us={DT_US} "
        f"finite={3 * RECEIVER_COUNT * SAMPLE_COUNT} nonzero={total_nonzero}"
    )
    print(f"max_abs={maximum_absolute:.12g}")
    for (name, _), energy in zip(digests, energies):
        print(f"energy_{name}={energy:.12g}")
    print(
        f"first_significant_s=[{min(first_times):.12g},{max(first_times):.12g}] "
        f"travel_lower_s=[{min(lower_bounds):.12g},{max(lower_bounds):.12g}] "
        f"travel_upper_s=[{min(upper_bounds):.12g},{max(upper_bounds):.12g}]"
    )
    for name, digest in digests:
        print(f"sha256_{name}={digest}")
    print("result=PASS")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: verify_overthrust_record.py OUTPUT_DIRECTORY", file=sys.stderr)
        return 2
    try:
        verify(Path(sys.argv[1]))
        return 0
    except (OSError, ValueError, UnicodeError, struct.error) as error:
        print(f"record verification failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
