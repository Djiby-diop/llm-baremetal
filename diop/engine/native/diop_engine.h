#ifndef DIOP_ENGINE_H
#define DIOP_ENGINE_H

#ifdef _WIN32
#define DIOP_EXPORT __declspec(dllexport)
#else
#define DIOP_EXPORT
#endif

// Opaque pointer to hide the internal C struct from Python
typedef void* DiopEngineState;

// Initializes the inference engine (allocates memory, loads weights)
// model_path: path to the .gguf or .bin file
DIOP_EXPORT DiopEngineState diop_engine_init(const char* model_path);

// Returns a JSON payload describing the model metadata discovered during init.
DIOP_EXPORT const char* diop_engine_inspect_model(DiopEngineState state);

// Returns a JSON payload describing the tensor graph and load plan compatibility.
DIOP_EXPORT const char* diop_engine_plan_load(DiopEngineState state);

// Returns a JSON payload describing runtime readiness for the next inference stage.
DIOP_EXPORT const char* diop_engine_prepare_runtime(DiopEngineState state);

// Generates text based on a prompt. Returns a newly allocated string (must be freed by caller if possible, or managed via zero-copy in the future).
DIOP_EXPORT const char* diop_engine_generate(DiopEngineState state, const char* prompt, int max_tokens);

// Frees the engine memory (clears RAM)
DIOP_EXPORT void diop_engine_free(DiopEngineState state);

#endif // DIOP_ENGINE_H
