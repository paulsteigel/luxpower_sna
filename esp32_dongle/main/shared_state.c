#include "shared_state.h"

reg_cache_t    g_regs      = {};
QueueHandle_t  g_write_queue = NULL;
event_flags_t  g_events    = {};
