"""Safety utilities: joint clamping, watchdog (future), graceful shutdown (future)."""


def clamp(values: list[float], limits: list[tuple[float, float]]) -> list[float]:
    """Clamp each value to its corresponding (min, max) range."""
    return [max(lo, min(hi, v)) for v, (lo, hi) in zip(values, limits)]


# Default stub limits: [-2, 2] rad (~[-115°, 115°]) — wider than any real joint
STUB_LIMITS = [(-2.0, 2.0)] * 12
