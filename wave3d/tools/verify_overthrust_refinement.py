#!/usr/bin/env python3
"""Verify the fixed dt versus dt/2 Overthrust SEG-Y refinement gate."""

from __future__ import annotations

import math
from pathlib import Path
import struct
import sys


THRESHOLD = 0.05
COMPONENT_CODES = (14, 13, 12)


class SegyRecord:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = path.read_bytes()
        if len(self.data) < 3600:
            raise ValueError(f"{path}: truncated SEG-Y headers")
        self.dt_us = struct.unpack_from(">H", self.data, 3216)[0]
        self.sample_count = struct.unpack_from(">H", self.data, 3220)[0]
        self.format_code = struct.unpack_from(">H", self.data, 3224)[0]
        if self.dt_us == 0 or self.sample_count == 0 or self.format_code != 5:
            raise ValueError(
                f"{path}: expected positive sampling and IEEE format code 5"
            )
        self.trace_bytes = 240 + 4 * self.sample_count
        payload_bytes = len(self.data) - 3600
        if payload_bytes % self.trace_bytes != 0:
            raise ValueError(f"{path}: trace payload is truncated")
        self.trace_count = payload_bytes // self.trace_bytes
        if self.trace_count == 0:
            raise ValueError(f"{path}: no traces")

        self.headers: list[bytes] = []
        self.samples: list[tuple[float, ...]] = []
        self.codes: list[int] = []
        for trace in range(self.trace_count):
            offset = 3600 + trace * self.trace_bytes
            header = self.data[offset : offset + 240]
            code = struct.unpack_from(">h", header, 28)[0]
            expected_code = COMPONENT_CODES[trace % len(COMPONENT_CODES)]
            if code != expected_code:
                raise ValueError(
                    f"{path}: trace {trace} has component code {code}, "
                    f"expected {expected_code}"
                )
            values = struct.unpack_from(
                f">{self.sample_count}f", self.data, offset + 240
            )
            if not all(math.isfinite(value) for value in values):
                raise ValueError(f"{path}: trace {trace} has non-finite samples")
            self.headers.append(header)
            self.samples.append(values)
            self.codes.append(code)


def require_matching_geometry(coarse: SegyRecord, fine: SegyRecord) -> None:
    if coarse.dt_us != 2 * fine.dt_us:
        raise ValueError("fine SEG-Y interval must be exactly dt/2")
    if fine.sample_count != 2 * coarse.sample_count:
        raise ValueError("fine SEG-Y must contain exactly twice as many samples")
    if coarse.trace_count != fine.trace_count or coarse.codes != fine.codes:
        raise ValueError("coarse/fine trace counts or component order differ")
    for trace, (coarse_header, fine_header) in enumerate(
        zip(coarse.headers, fine.headers)
    ):
        # Bytes 115-118 (one-based) are the per-trace sample count/interval.
        if coarse_header[:114] != fine_header[:114] or coarse_header[118:] != fine_header[118:]:
            raise ValueError(f"trace {trace} acquisition headers differ")


def squared_sums(
    coarse: SegyRecord, fine: SegyRecord, component: int | None
) -> tuple[float, float]:
    trace_indices = (
        range(coarse.trace_count)
        if component is None
        else range(component, coarse.trace_count, 3)
    )
    differences: list[float] = []
    references: list[float] = []
    for trace in trace_indices:
        coarse_trace = coarse.samples[trace]
        fine_trace = fine.samples[trace]
        for sample, coarse_value in enumerate(coarse_trace):
            fine_value = fine_trace[2 * sample + 1]
            difference = coarse_value - fine_value
            differences.append(difference * difference)
            references.append(fine_value * fine_value)
    return math.fsum(differences), math.fsum(references)


def normalized_l2(
    coarse: SegyRecord, fine: SegyRecord, component: int | None
) -> float:
    difference_squared, reference_squared = squared_sums(
        coarse, fine, component
    )
    if reference_squared == 0.0:
        raise ValueError("fine-grid reference energy is zero")
    return math.sqrt(difference_squared / reference_squared)


def main() -> int:
    if len(sys.argv) != 3:
        print(
            "usage: verify_overthrust_refinement.py COARSE.sgy FINE.sgy",
            file=sys.stderr,
        )
        return 2
    try:
        coarse = SegyRecord(Path(sys.argv[1]))
        fine = SegyRecord(Path(sys.argv[2]))
        require_matching_geometry(coarse, fine)
        overall = normalized_l2(coarse, fine, None)
        components = [
            normalized_l2(coarse, fine, component) for component in range(3)
        ]
        print(
            f"traces={coarse.trace_count} coarse_samples={coarse.sample_count} "
            f"fine_samples={fine.sample_count} coarse_dt_us={coarse.dt_us} "
            f"fine_dt_us={fine.dt_us}"
        )
        print(f"normalized_l2_all={overall:.12g}")
        for name, value in zip(("vx", "vy", "vz"), components):
            print(f"normalized_l2_{name}={value:.12g}")
        print(f"threshold={THRESHOLD:.12g}")
        if overall > THRESHOLD:
            print("result=FAIL", file=sys.stderr)
            return 1
        print("result=PASS")
        return 0
    except (OSError, ValueError, struct.error) as error:
        print(f"refinement verification failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
