# djib/models/

Place GGUF model files here for DIOP inference via DiopLlamaAdapter.

Preferred file names (checked in order):
  djibion.gguf         - Djibion Ultramodel (primary)
  djibion-q4.gguf      - Quantized Q4 variant
  djibion-q8.gguf      - Quantized Q8 variant
  phi-3-mini.gguf      - Phi-3-mini fallback
  tinyllama.gguf       - TinyLlama fallback

Also searched: djib/llama.cpp/models/, diop/models/

To download (when available on HuggingFace):
  huggingface-cli download djibydiop/llm-baremetal djibion.gguf --local-dir djib/models/
