from bridge import NativeEngineBridge

def main():
    print("=== TEST DU MOTEUR C BARE-METAL DEPUIS PYTHON ===")
    
    # 1. Charger le pont FFI
    engine = NativeEngineBridge()
    
    # 2. Demander au moteur C de charger un vrai modèle GGUF local
    engine.load_model(r"C:\Users\djibi\OneDrive\Bureau\baremetal\llm-baremetal\models\tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf")
    
    # 3. Exécuter l'inférence 100% en C !
    print("\n--- Début de la Génération ---")
    reponse = engine.generate("Quelle est la vitesse de la RAM ?", max_tokens=100)
    print("\n--- Réponse du Moteur C ---")
    print(reponse)
    print("----------------------------\n")
    print("Si tu vois ce message, c'est que le Python a réussi à communiquer directement avec le binaire C compilé !")

if __name__ == "__main__":
    main()
