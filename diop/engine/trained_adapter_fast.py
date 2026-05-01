
import torch
import torch.nn as nn
from .trained_adapter import TrainedModelAdapter

class FastTrainedModelAdapter(TrainedModelAdapter):
    """
    Optimized version of the adapter with faster inference.
    """
    
    def _generate_tokens(self, model, prompt_ids, cfg, max_new, tok):
        device = next(model.parameters()).device
        ids = torch.tensor([prompt_ids], dtype=torch.long, device=device)
        
        # Initialisation du cache
        # Note: Cette implémentation dépend de si le modèle supporte le cache.
        # Comme on utilise nn.TransformerEncoderLayer, on va simuler un cache 
        # ou utiliser torch.inference_mode() pour l'instant.
        
        print("\n[Inférence Optimisée] ", end="", flush=True)
        
        generated = []
        with torch.inference_mode():
            # Première passe sur tout le prompt
            logits = model(ids)
            next_id = int(torch.multinomial(torch.softmax(logits[:, -1, :] / 0.1, dim=-1), 1))
            
            generated.append(next_id)
            print(tok.decode([next_id]), end="", flush=True)
            
            # Passes suivantes
            for _ in range(max_new - 1):
                # On ne garde que les derniers tokens pour limiter le calcul
                # (Une vraie implémentation KV-cache modifierait le forward du modèle)
                context = ids[:, -cfg.seq_len:]
                logits = model(context)
                next_id = int(torch.multinomial(torch.softmax(logits[:, -1, :] / 0.1, dim=-1), 1))
                
                ids = torch.cat([ids, torch.tensor([[next_id]], device=device)], dim=1)
                generated.append(next_id)
                char = tok.decode([next_id])
                print(char, end="", flush=True)
                
                if next_id == 0: break # End of text
                
        return tok.decode(generated)
