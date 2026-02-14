import functools
import struct

import wasmtime


class WASMModule:
    def __init__(self, wasm, *, store, wasi=False):
        self._store = store

        linker = wasmtime.Linker(self._store.engine)
        if wasi:
            self._store.set_wasi(wasmtime.WasiConfig())
            linker.define_wasi()

        self._instance = linker.instantiate(
            self._store,
            wasmtime.Module(self._store.engine, wasm),
        )
        self._memory = self._instance.exports(self._store)["memory"]

        exports = self._instance.exports(self._store)
        if "_initialize" in exports:
            exports["_initialize"](store)
        elif "__wasm_call_ctors" in exports:
            exports["__wasm_call_ctors"](store)

    def __getattr__(self, name):
        exports = self._instance.exports(self._store)

        try:
            export = exports[name]
        except KeyError as e:
            raise AttributeError(
                f"{type(self).__name__} has no attribute {name!r} (no such WASM export)"
            ) from e

        bound = functools.partial(export, self._store)
        setattr(self, name, bound)

        return bound

    @property
    def store(self):
        return self._store

    def read_struct(self, fmt, ptr):
        return struct.unpack_from(fmt, self._memory.get_buffer_ptr(self._store), ptr)

    def write_struct(self, fmt, ptr, *values):
        self._memory.write(self._store, struct.pack(fmt, *values), ptr)
