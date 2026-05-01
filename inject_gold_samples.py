
import json
import uuid
from pathlib import Path

def create_gold_sample(goal, worker, summary, artifacts=None, risks=None):
    task_id = f"gold_{uuid.uuid4().hex[:8]}"
    result = {
        "task_id": task_id,
        "worker": worker,
        "status": "completed",
        "summary": summary,
        "artifacts": artifacts or [],
        "risks": risks or [],
        "recommendations": ["Use zero-copy buffers", "Verify PCI Command Register"],
        "needs_validation": False,
        "metadata": {"source": "gold_injection"}
    }
    return {
        "id": task_id,
        "type": "successful_task",
        "content": {
            "task": {"goal": goal, "id": task_id},
            "result": result
        }
    }

def main():
    pattern_path = Path("diop/runtime/memory/pattern.jsonl")
    pattern_path.parent.mkdir(parents=True, exist_ok=True)
    
    samples = []
    
    # Gold Sample 1: PCI Compliance (The fix for your e1000)
    samples.append(create_gold_sample(
        goal="Clarify request: [OO-Driver] PCI Compliance for e1000",
        worker="analysis",
        summary="PCI compliance checklist: Enable Bus Master and Memory Space bits.",
        risks=["Memory corruption if MMIO accessed without enabling"],
        artifacts=[{"name": "pci_fix.h", "type": "code", "content": "#define PCI_COMMAND 0x04\n#define PCI_CMD_MEM (1 << 1)\n#define PCI_CMD_MASTER (1 << 2)"}]
    ))

    # Gold Sample 2: Multi-instance Safety
    samples.append(create_gold_sample(
        goal="Design solution for: Multi-instance driver support",
        worker="architecture",
        summary="Use a device context structure instead of static globals.",
        artifacts=[{"name": "driver_ctx.h", "type": "code", "content": "typedef struct { uint32_t mmio; int irq; } OoDriverCtx;"}]
    ))
    
    # Gold Sample 3: Bare-metal UART
    samples.append(create_gold_sample(
        goal="Produce implementation outline for: UART16550 init",
        worker="code",
        summary="Initialize 16550 UART with 115200 baudrate.",
        artifacts=[{"name": "uart.c", "type": "code", "content": "void uart_init(int port) { outb(port + 3, 0x80); outb(port + 0, 0x01); }"}]
    ))

    # Injection massive (clonage de variantes pour renforcer les poids)
    with pattern_path.open("a", encoding="utf-8") as f:
        for s in samples:
            # On injecte chaque échantillon 20 fois pour "forcer" la mémorisation
            for _ in range(20):
                f.write(json.dumps(s) + "\n")
                
    print(f"[Gold Injection] Injected {len(samples) * 20} records into {pattern_path}")

if __name__ == "__main__":
    main()
