#include "soma_mind.h"
#include "soma_router.h"
#include "soma_dna.h"
#include "../../../united-baremetal/include/united_bus.h"
#include "../../../oo-modules/mercatorion-engine/core/mercatorion.h"
#include "../../../oo-modules/symbion-engine/core/symbion.h"

/**
 * OO_ORGANISM_IGNITE
 * 
 * The primordial function that brings the Operating Organism to life.
 */

void oo_organism_ignite(SomaMindCtx *m, 
                        SomaRouterCtx *router,
                        Genomion *genomion,
                        MercatorCtx *mercator,
                        SymbionCtx *symbion,
                        OverclockCtx *overclock) {
    
    // 1. Initialize the Nervous System (United-Bus)
    // Already handled by global init or UEFI bridge
    
    // 2. Hardware Symbiosis (Symbion)
    symbion_scan_body(symbion);

    // 2.5 Performance Ignition (Overclock)
    overclock_init(overclock);
    overclock_tune_cpu(overclock);
    
    // 3. Identity Generation (Genomion)
    if (soma_genomion_validate(genomion) == 0) {
        soma_genomion_init_default(genomion);
    }
    
    // 4. Sensory Gating (Vomerion)
    soma_router_init(router, genomion);
    
    // 5. Financial Instinct (Mercatorion)
    mercator_init(mercator);
    mercator->genomion = genomion; // Link DNA to trading
    
    // 6. The Mind (Soma-Mind)
    soma_mind_init(m, router, NULL, NULL, NULL, NULL, NULL, genomion, mercator, symbion);
    
    // 7. First Breath (Homeostasis)
    m->active = 1;
    _log_causal(0, "organism_ignited_successfully");
    _log_causal(0, "genomion_seed_active");
}
