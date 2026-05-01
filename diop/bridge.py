import socket
import urllib.request
import urllib.error
import json
import time
import threading

QEMU_TCP_IP = '127.0.0.1'
QEMU_TCP_PORT = 4444
DIOP_GATEWAY_URL = 'http://127.0.0.1:11434/api/generate'
MODEL_NAME = 'djibion'

def query_djibion(prompt: str) -> str:
    """Send the prompt to the DIOP gateway (Ollama API compatible)."""
    data = {
        "model": MODEL_NAME,
        "prompt": prompt,
        "stream": False
    }
    req = urllib.request.Request(DIOP_GATEWAY_URL, data=json.dumps(data).encode('utf-8'))
    req.add_header('Content-Type', 'application/json')
    
    try:
        response = urllib.request.urlopen(req)
        result = json.loads(response.read().decode('utf-8'))
        return result.get('response', '')
    except urllib.error.URLError as e:
        return f"[ERREUR] Le pont avec Djibion est rompu: {e}"

def bridge_loop():
    print(f"[*] Démarrage du Synapse Télépathique...")
    print(f"[*] Attente de la naissance de l'Organisme sur {QEMU_TCP_IP}:{QEMU_TCP_PORT}...")
    
    while True:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((QEMU_TCP_IP, QEMU_TCP_PORT))
            print("[+] Connexion établie avec l'Organisme (UART) !")
            
            buffer = ""
            while True:
                data = s.recv(1024)
                if not data:
                    break
                    
                buffer += data.decode('utf-8', errors='ignore')
                
                # Check if we have a full line
                if '\n' in buffer:
                    lines = buffer.split('\n')
                    # Keep the incomplete part in the buffer
                    buffer = lines.pop()
                    
                    for line in lines:
                        clean_line = line.strip()
                        print(f"[OS] {clean_line}") # Pour qu'on voie ce que l'OS fait
                        
                        # Trigger de la télépathie
                        if clean_line.startswith("[DJIBION_REQ]"):
                            prompt = clean_line.replace("[DJIBION_REQ]", "").strip()
                            print(f"\n[SYNAPSE] Question captée : '{prompt}'")
                            print(f"[SYNAPSE] Interrogation de Djibion en cours...")
                            
                            answer = query_djibion(prompt)
                            
                            print(f"[SYNAPSE] Réponse reçue. Injection dans l'Organisme...")
                            
                            # On renvoie la réponse formatée à l'OS via le port Série
                            # On ajoute un marqueur de début et de fin pour que le C puisse parser facilement
                            payload = f"[DJIBION_RES]{answer}[DJIBION_END]\n"
                            
                            # On envoie par petits morceaux pour ne pas saturer le buffer UART de QEMU
                            for chunk in [payload[i:i+64] for i in range(0, len(payload), 64)]:
                                s.send(chunk.encode('utf-8'))
                                time.sleep(0.01) # Laisse le temps à l'OS de digérer
                                
        except ConnectionRefusedError:
            time.sleep(2) # QEMU n'est pas encore lancé
        except Exception as e:
            print(f"[-] Erreur de connexion: {e}")
            time.sleep(2)
        finally:
            s.close()

if __name__ == "__main__":
    bridge_loop()
