#ifndef EXT_MERCENARY_H
#define EXT_MERCENARY_H

#include "ext.h"

struct ext_state *ext_register_mercenary(void);

int mercenary_code_injections(int pc, int op);

#endif  /* EXT_MERCENARY_H */
