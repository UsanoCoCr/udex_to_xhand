"""Safety utilities: joint clamping, watchdog (future), graceful shutdown (future)."""

import math


def clamp(values: list[float], limits: list[tuple[float, float]]) -> list[float]:
    """Clamp each value to its corresponding (min, max) range."""
    return [max(lo, min(hi, v)) for v, (lo, hi) in zip(values, limits)]


# Last-resort hard limits in radians (XHand physical range: -90° to 110°).
# Primary clamping is done in degree domain inside JointMapper.
_D2R = math.pi / 180.0
HARD_LIMITS_RAD = [(-90 * _D2R, 110 * _D2R)] * 12
