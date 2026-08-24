/*
 * PC port shim for bare <R4300.h> includes (some game files include it
 * without the PR/ prefix, which resolves to include/PR/R4300.h and would
 * bypass the PHYS_TO_K0 redefinition in port/shim/PR/R4300.h). Same content
 * as that shim; keep the two in sync.
 */
#ifndef _PORT_SHIM_R4300_BARE_H_
#define _PORT_SHIM_R4300_BARE_H_

#if defined(PORT)
#    include "include/PR/R4300.h"

#    undef PHYS_TO_K0
#    define PHYS_TO_K0(x) ((u32)(x)) /* identity: V1 view is already virtual */

#else
#    include <PR/R4300.h>
#endif

#endif /* _PORT_SHIM_R4300_BARE_H_ */
