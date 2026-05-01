import sys
import torch
from pathlib import Path
from diop.engine.trained_adapter_fast import FastTrainedModelAdapter
from diop.adapters.base import GenerationRequest

def diagnose():
    # Load model using the FAST adapter
    adapter = FastTrainedModelAdapter()

def main():
    print("=== DIAGNOSTIC DIOP-CORE MINI (83MB) ===")
    adapter = FastTrainedModelAdapter()
    
    # On va monkeypatcher la génération pour afficher les lettres une par une
    def progressive_generate(model, prompt_ids, cfg, max_new, tok):
        ids = list(prompt_ids)
        temperature = 0.7
        generated_text = ""
        print("\n[Génération en cours] ", end="", flush=True)
        
        with torch.no_grad():
            for i in range(max_new):
                context = ids[-cfg.seq_len:]
                inp = torch.tensor([context], dtype=torch.long)
                logits = model(inp)[:, -1, :] / temperature
                probs = torch.softmax(logits, dim=-1)
                next_id = int(torch.multinomial(probs, num_samples=1))
                
                ids.append(next_id)
                char = tok.decode([next_id])
                generated_text += char
                print(char, end="", flush=True) # Affiche en temps réel
                
                if next_id == 0: break
        print("\n")
        return generated_text

    adapter._generate_tokens = progressive_generate
    
    request = GenerationRequest(
        worker="planner",
        task_goal="Clarify request: [OO-Driver] Generate minimal bare-metal C driver stub for PCI vendor=0x8086 device=0x100E class=Network.",
        instructions=["Produce a short JSON plan."],
        mode="auto"
    )
    
    try:
        response = adapter.generate(request)
        print("\n--- RÉSULTAT FINAL ---")
        print(f"Summary: {response.summary}")
    except Exception as e:
        print(f"\n[ERREUR] : {e}")

if __name__ == "__main__":
    main()
