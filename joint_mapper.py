"""Joint mapping: UDCAP 24 DOF → XHand 12 DOF per hand.

Stub: takes first 12 params, flips sign, converts deg→rad.
Real config-driven mapping implemented in M4.
"""

import math


class JointMapper:
    def __init__(self, config: dict):
        self._config = config

    def map(self, hand: str, udcap_24: list[float]) -> list[float]:
        """Map 24 UDCAP params to 12 XHand joint positions in radians.

        Stub: take indices [0:12], flip sign (UDCAP neg=flex → XHand pos=flex),
        convert degrees to radians.
        """
        raw_12 = udcap_24[:12]
        return [(-v) * math.pi / 180.0 for v in raw_12]


if __name__ == "__main__":
    mapper = JointMapper({})
    test_input = list(range(-60, -60 + 24))
    result = mapper.map("left", test_input)
    print(f"Input  (24, deg): {test_input}")
    print(f"Output (12, rad): {[round(v, 4) for v in result]}")
