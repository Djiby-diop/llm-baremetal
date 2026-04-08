# oo-sim — OO Simulator Environment

Simulateur QEMU pour tester le kernel OO bare-metal sans vrai hardware.

## Structure

```
oo-sim/
├── core/       — Scripts de boot QEMU (PowerShell + bash)
├── scenarios/  — Scénarios de test automatisés
└── README.md
```

## Scénarios disponibles

| Scénario | Description | Commande |
|----------|-------------|---------|
| `boot-basic` | Boot standard + /ssm_infer test | `make sim` |
| `boot-halt-calibration` | Test calibration halt head | `make sim-halt` |
| `boot-multicore` | Test SMP (AP wakeup) | `make sim-smp` |
| `boot-crash-recovery` | Déclenche thanatosion | `make sim-crash` |
| `boot-mirrorion` | Test introspection idle | `make sim-mirror` |

## Utilisation rapide

```powershell
# Boot QEMU avec le dernier kernel
.\test-qemu-v3.ps1

# Boot avec scénario spécifique
.\oo-sim\scenarios\halt_calibration.ps1
```

## Pré-requis

- QEMU 8.x avec OVMF UEFI firmware
- `C:\Temp\ovmf_vars.fd` (copie de edk2-i386-vars.fd)
- Image USB: `C:\Temp\oo_usb_v3_gpt.img` (rebuild: `.\flash-usb-v3.ps1`)
