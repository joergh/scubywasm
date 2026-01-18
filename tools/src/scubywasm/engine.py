import dataclasses

from .common import Config, Pose
from .wasmmodule import WASMModule


class Engine:
    def __init__(
        self,
        wasm,
        *,
        store,
        engine_cfg=None,
    ):
        self._module = WASMModule(wasm, store=store)
        self._ctx = self._create_context(engine_cfg)
        self._pose_ptr = self._module.get_pose_buffer()

    def _create_context(self, engine_cfg):
        cfg_ptr = self._module.get_config_buffer()

        cfg_keys = [
            "ship_max_turn_rate",
            "ship_max_velocity",
            "ship_hit_radius",
            "shot_velocity",
            "shot_lifetime",
        ]
        if engine_cfg is None:
            self._module.set_default_config(cfg_ptr)
            cfg_values = self._module.read_struct("<ffffi", cfg_ptr)
            self._cfg = Config(
                **{key: value for key, value in zip(cfg_keys, cfg_values)}
            )
        else:
            self._cfg = engine_cfg

            cfg_as_dict = dataclasses.asdict(engine_cfg)
            self._module.write_struct(
                "<ffffi", cfg_ptr, *[cfg_as_dict[key] for key in cfg_keys]
            )

        return self._module.create_context(cfg_ptr)

    @property
    def config(self):
        return self._cfg

    def add_agent(self, pose):
        self._module.write_struct("<fff", self._pose_ptr, pose.x, pose.y, pose.heading)
        return self._module.add_agent(self._ctx, self._pose_ptr)

    def set_action(self, agent_id, action):
        self._module.set_action(self._ctx, agent_id, action)

    def tick(self, n_times):
        return self._module.tick(self._ctx, n_times)

    def get_ship_pose(self, agent_id):
        self._module.get_ship_pose(self._ctx, agent_id, self._pose_ptr)
        x, y, heading = self._module.read_struct("<fff", self._pose_ptr)
        return Pose(x=x, y=y, heading=heading)

    def get_shot_pose(self, agent_id):
        lifetime = self._module.get_shot_pose(self._ctx, agent_id, self._pose_ptr)
        x, y, heading = self._module.read_struct("<fff", self._pose_ptr)
        return Pose(x=x, y=y, heading=heading), lifetime

    def is_alive(self, agent_id):
        return self._module.is_alive(self._ctx, agent_id) == 1

    def get_score(self, agent_id):
        return self._module.get_score(self._ctx, agent_id)
