import ctypes
import sys
from pathlib import Path


class NativeEngineBridge:
    """
    The Cortex-to-Soma FFI Bridge.
    Connects DIOP Python logic directly to the C bare-metal engine via a shared library.
    No HTTP, no JSON serialization over network, pure memory pointers.
    """
    
    def __init__(self):
        self._state = None
        self.lib = None
        self.lib_path = self._find_library()
        self.lib = ctypes.CDLL(str(self.lib_path))
        
        # Setup ABI (Application Binary Interface) signatures
        # 1. diop_engine_init
        self.lib.diop_engine_init.argtypes = [ctypes.c_char_p]
        self.lib.diop_engine_init.restype = ctypes.c_void_p
        
        # 2. diop_engine_generate
        self.lib.diop_engine_inspect_model.argtypes = [ctypes.c_void_p]
        self.lib.diop_engine_inspect_model.restype = ctypes.c_char_p

        self.lib.diop_engine_plan_load.argtypes = [ctypes.c_void_p]
        self.lib.diop_engine_plan_load.restype = ctypes.c_char_p

        self.lib.diop_engine_prepare_runtime.argtypes = [ctypes.c_void_p]
        self.lib.diop_engine_prepare_runtime.restype = ctypes.c_char_p

        self.lib.diop_engine_generate.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        self.lib.diop_engine_generate.restype = ctypes.c_char_p
        
        # 3. diop_engine_free
        self.lib.diop_engine_free.argtypes = [ctypes.c_void_p]
        self.lib.diop_engine_free.restype = None
        

    @staticmethod
    def _resolve_library_path() -> Path:
        base_dir = Path(__file__).resolve().parent / "native"
        if sys.platform == "win32":
            return base_dir / "diop_engine.dll"
        elif sys.platform == "darwin":
            return base_dir / "libdiop_engine.dylib"
        else:
            return base_dir / "libdiop_engine.so"

    def _find_library(self) -> Path:
        return self._resolve_library_path()

    @classmethod
    def library_path(cls) -> Path:
        return cls._resolve_library_path()

    @classmethod
    def probe_runtime(cls) -> dict[str, object]:
        lib_path = cls._resolve_library_path()

        info: dict[str, object] = {
            "platform": sys.platform,
            "library_path": str(lib_path),
            "library_exists": lib_path.exists(),
            "status": "missing",
            "error": "",
        }
        if not lib_path.exists():
            return info

        try:
            ctypes.CDLL(str(lib_path))
            info["status"] = "ready"
            return info
        except Exception as e:
            info["status"] = "unavailable"
            info["error"] = str(e)
            return info
            
    def load_model(self, model_path: str):
        print(f"[Cortex FFI] Signaling Soma to load {model_path}...")
        b_path = model_path.encode("utf-8")
        self._state = self.lib.diop_engine_init(b_path)
        if not self._state:
            raise RuntimeError("C Engine failed to initialize.")

    def inspect_model(self) -> str:
        if not self._state:
            raise RuntimeError("Model not loaded. Call load_model() first.")
        return self._decode_c_string(self.lib.diop_engine_inspect_model(self._state))

    def plan_load(self) -> str:
        if not self._state:
            raise RuntimeError("Model not loaded. Call load_model() first.")
        return self._decode_c_string(self.lib.diop_engine_plan_load(self._state))

    def prepare_runtime(self) -> str:
        if not self._state:
            raise RuntimeError("Model not loaded. Call load_model() first.")
        return self._decode_c_string(self.lib.diop_engine_prepare_runtime(self._state))
            
    def generate(self, prompt: str, max_tokens: int = 512) -> str:
        if not self._state:
            raise RuntimeError("Model not loaded. Call load_model() first.")
            
        b_prompt = prompt.encode("utf-8")
        print("[Cortex FFI] Pointers linked. Firing C inference loop...")
        
        # This blocks the Python thread and executes purely in C!
        c_result = self.lib.diop_engine_generate(self._state, b_prompt, max_tokens)
        return self._decode_c_string(c_result)

    @staticmethod
    def _decode_c_string(c_string_pointer) -> str:
        return ctypes.string_at(c_string_pointer).decode("utf-8")

    def __del__(self):
        if self._state and self.lib is not None:
            self.lib.diop_engine_free(self._state)
