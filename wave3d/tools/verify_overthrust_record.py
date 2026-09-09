#!/usr/bin/env python3
"""Independently verify the fixed production Overthrust SEG-Y record."""

from __future__ import annotations

import hashlib
import math
from pathlib import Path
import struct
import sys


RECEIVER_SIDE = 11
RECEIVER_COUNT = RECEIVER_SIDE * RECEIVER_SIDE
TRACE_COUNT = RECEIVER_COUNT * 3
SAMPLE_COUNT = 3000
DT_US = 1000
COMPONENT_CODES = (14, 13, 12)
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


def verify(output_directory: Path) -> None:
    entries = sorted(path.name for path in output_directory.iterdir())
    if entries != ["record.sgy"]:
        raise ValueError(
            f"output directory must contain only record.sgy, found {entries}"
        )
    path = output_directory / "record.sgy"
    data = path.read_bytes()
    if len(data) < 3600:
        raise ValueError("record.sgy is shorter than its SEG-Y headers")

    text = data[:3200].decode("ascii")
    required_text = (
        "SEG-Y REVISION 1, BIG-ENDIAN",
        "TRACE ORDER: RECEIVER-MAJOR, THEN VX,VY,VZ",
        "VX = IN-LINE/EAST",
        "VY = CROSS-LINE/NORTH",
        "VZ = VERTICAL/DOWN",
        "NORMALIZATION NONE",
        "TIME AXIS IS (N+1)*DT",
    )
    if not all(item in text for item in required_text):
        raise ValueError("SEG-Y textual header lost required interpretation")

    binary_checks = {
        "job identification": (i32(data, 3200), 1),
        "traces per ensemble": (i16(data, 3212), TRACE_COUNT),
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
            raise ValueError(f"SEG-Y {name} is {actual}, expected {expected}")

    trace_bytes = 240 + 4 * SAMPLE_COUNT
    expected_bytes = 3600 + TRACE_COUNT * trace_bytes
    if len(data) != expected_bytes:
        raise ValueError(
            f"record.sgy has {len(data)} bytes, expected {expected_bytes}"
        )

    finite_count = 0
    nonzero_count = 0
    maximum_absolute = 0.0
    component_squares: list[list[float]] = [[], [], []]
    receiver_samples: list[list[tuple[float, ...]]] = []
    for receiver in range(RECEIVER_COUNT):
        expected_location = expected_receiver(receiver)
        triplet: list[tuple[float, ...]] = []
        for component in range(3):
            trace = receiver * 3 + component
            offset = 3600 + trace * trace_bytes
            header = data[offset : offset + 240]
            sequence = trace + 1
            sequence_fields = tuple(i32(header, field) for field in (0, 4, 12, 24))
            if sequence_fields != (sequence, sequence, sequence, sequence):
                raise ValueError(f"trace {trace} sequence fields are inconsistent")
            if i32(header, 8) != 1 or i32(header, 16) != 1 or i32(header, 20) != 1:
                raise ValueError(f"trace {trace} source/ensemble fields are inconsistent")
            if i16(header, 28) != COMPONENT_CODES[component]:
                raise ValueError(f"trace {trace} component code is incorrect")
            if i16(header, 68) != -1000 or i16(header, 70) != -1000:
                raise ValueError(f"trace {trace} coordinate scalars are incorrect")
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
            if source != SOURCE or receiver_location != expected_location:
                raise ValueError(f"trace {trace} source/receiver coordinates changed")
            if i16(header, 88) != 1:
                raise ValueError(f"trace {trace} coordinate units are not metres")
            if u16(header, 114) != SAMPLE_COUNT or u16(header, 116) != DT_US:
                raise ValueError(f"trace {trace} sampling header changed")

            values = struct.unpack_from(
                f">{SAMPLE_COUNT}f", data, offset + 240
            )
            if not all(math.isfinite(value) for value in values):
                raise ValueError(f"trace {trace} contains a non-finite sample")
            finite_count += len(values)
            nonzero_count += sum(value != 0.0 for value in values)
            maximum_absolute = max(
                maximum_absolute, max(abs(value) for value in values)
            )
            component_squares[component].extend(value * value for value in values)
            triplet.append(values)
        receiver_samples.append(triplet)

    first_times: list[float] = []
    lower_bounds: list[float] = []
    upper_bounds: list[float] = []
    dt_s = DT_US * 1.0e-6
    for receiver, components in enumerate(receiver_samples):
        envelope = [
            max(abs(components[axis][sample]) for axis in range(3))
            for sample in range(SAMPLE_COUNT)
        ]
        peak = max(envelope)
        if peak == 0.0:
            raise ValueError(f"receiver {receiver} has zero three-component data")
        first_sample = next(
            sample
            for sample, value in enumerate(envelope)
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

    energies = [math.fsum(values) for values in component_squares]
    if nonzero_count == 0 or not all(energy > 0.0 for energy in energies):
        raise ValueError("record has zero total or component energy")
    print(
        f"bytes={len(data)} traces={TRACE_COUNT} samples_per_trace={SAMPLE_COUNT} "
        f"dt_us={DT_US} finite={finite_count} nonzero={nonzero_count}"
    )
    print(f"max_abs={maximum_absolute:.12g}")
    for name, energy in zip(("vx", "vy", "vz"), energies):
        print(f"energy_{name}={energy:.12g}")
    print(
        f"first_significant_s=[{min(first_times):.12g},{max(first_times):.12g}] "
        f"travel_lower_s=[{min(lower_bounds):.12g},{max(lower_bounds):.12g}] "
        f"travel_upper_s=[{min(upper_bounds):.12g},{max(upper_bounds):.12g}]"
    )
    print(f"sha256={hashlib.sha256(data).hexdigest()}")
    print("result=PASS")


def main() -> int:
    if len(sys.argv) != 2:
        print(
            "usage: verify_overthrust_record.py OUTPUT_DIRECTORY",
            file=sys.stderr,
        )
        return 2
    try:
        verify(Path(sys.argv[1]))
        return 0
    except (OSError, ValueError, UnicodeError, struct.error) as error:
        print(f"record verification failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
