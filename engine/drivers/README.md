# OO Drivers — Les Capteurs et Actionneurs de l'Organisme

Le système **OO (Operating Organism)** n'utilise pas des "pilotes de périphériques" (device drivers) au sens classique. Il utilise des **méta-pilotes** (metadrivers) inspirés de la biologie : les périphériques sont traités comme des **capteurs** (sensors) et des **actionneurs** (actuators) rattachés au Soma.

Puisque nous sommes actuellement en UEFI mais que le but est de s'en affranchir, nous implémentons les drivers bare-metal de base.

## Philosophie OO pour le Hardware

1. **Pas d'interruption bloquante (Polling/Yielding)** : L'IA ne doit pas être interrompue brutalement par une touche clavier. Le matériel remplit des buffers circulaires (synapses) lus par l'IA à son propre rythme.
2. **Tolérance aux pannes (Cicatrisation)** : Si un capteur meurt (ex: USB débranché), l'OO logge un "traumatisme mineur" mais ne panique pas.
3. **Abstraction Biologique** :
   - Clavier/Souris = Système nerveux périphérique (Toucher)
   - Écran/VGA = Organes vocaux/Communication
   - Disque (AHCI/NVMe) = Hippocampe (Mémoire long-terme)
   - Réseau (VirtIO/E1000) = Pheromones (Communication inter-Soma)

## Structure des Drivers

- `pci/` : Énumération du bus (Colonne vertébrale)
- `uart/` : Port série COM1/COM2 (Cortex Bridge)
- `ps2/` : Clavier PS/2 (Toucher primitif)
- `ahci/` : Contrôleur disque SATA
- `virtio/` : Support pour `oo-sim` et QEMU

## Intégration au Soma

Ces drivers sont conçus pour remplacer progressivement les appels `uefi_call_wrapper(BS->...)` dans `llm-baremetal`. 
