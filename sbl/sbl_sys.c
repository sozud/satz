#include "sega_xpt.h"

void SYS_SetUintMacSave(Uint32 vector, void (*handler)(void)) { satz_scu_vector_set(vector, handler); }
void (*SYS_GetUintMacSave(Uint32 vector))(void) { return satz_scu_vector_get(vector); }

void satz_bios_execdmp(void) { satz_die("SYS_EXECDMP: the program exited to the BIOS"); }
