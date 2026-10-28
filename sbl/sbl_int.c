#include "sega_xpt.h"
#include "sega_int.h"

void INT_SetScuFunc(int n, void (*handler)(void)) { satz_scu_vector_set((unsigned int)n, handler); }
void (*INT_GetScuFunc(int n))(void) { return satz_scu_vector_get((unsigned int)n); }
