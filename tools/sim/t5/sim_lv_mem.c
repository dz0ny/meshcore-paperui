// Host stand-in for src/lv_mem_psram.c. lv_conf.h sets LV_USE_STDLIB_MALLOC =
// LV_STDLIB_CUSTOM, so LVGL needs these *_core hooks provided by the app. On
// device they route to PSRAM via heap_caps; on the host they're plain stdlib.
#include "lvgl.h"
#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#include <stdlib.h>

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void* mem, size_t bytes) { LV_UNUSED(mem); LV_UNUSED(bytes); return NULL; }
void lv_mem_remove_pool(lv_mem_pool_t pool) { LV_UNUSED(pool); }

void* lv_malloc_core(size_t size) { return malloc(size); }
void* lv_realloc_core(void* p, size_t new_size) { return realloc(p, new_size); }
void  lv_free_core(void* p) { free(p); }

void lv_mem_monitor_core(lv_mem_monitor_t* mon_p) { LV_UNUSED(mon_p); }
lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }

#endif
