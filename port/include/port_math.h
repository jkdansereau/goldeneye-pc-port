/* port_math.h -- real system math declarations for port/ code.
 *
 * The build's -I list puts the game's include/math.h (a constants-only
 * header: M_PI & friends, guard _MATH_EXT_H_) ahead of the system one, so a
 * plain #include <math.h> in a .c file picks up the game header and leaves
 * fabs/tan/atan/pow/... implicitly declared. Including from THIS header lets
 * #include_next step past the game header to the real system math.h.
 */
#ifndef PORT_MATH_H
#define PORT_MATH_H

#include <math.h>      /* -> include/math.h (game constants) via -I include  */
#include_next <math.h> /* -> next match in the search path: system math.h    */

#endif /* PORT_MATH_H */
