
#include "diagnostion.h"

void diagnostion_init(DiagnostionEngine *e) {
    if (!e) return;
    e->mode = DIAGNOSTION_MODE_ON;
    e->reports_written = 0;
}

void diagnostion_set_mode(DiagnostionEngine *e, DiagnostionMode mode) {
    if (!e) return;
    e->mode = mode;
}

const char *diagnostion_mode_name_ascii(DiagnostionMode mode) {
    if (mode == DIAGNOSTION_MODE_OFF) return "off";
    if (mode == DIAGNOSTION_MODE_ON) return "on";
    return "?";
}
