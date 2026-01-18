import dataclasses


@dataclasses.dataclass(frozen=True)
class Config:
    ship_max_turn_rate: float
    ship_max_velocity: float
    ship_hit_radius: float
    shot_velocity: float
    shot_lifetime: int


@dataclasses.dataclass(frozen=True)
class Pose:
    x: float
    y: float
    heading: float
