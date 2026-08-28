#include <ultra64.h>
#include "chrobjdata.h"

void init_player_gait_object(void) {
#ifdef PORT
  /* PC port (D86, docs/PCPortResearch.md): the N64 source does `(int)&player_gait_hdr`,
   * a same-width (32<-32) pointer->int->pointer round trip that's a no-op on N64.
   * On PC the int cast truncates a real 64-bit pointer to its low 32 bits, and the
   * implicit int->pointer conversion back into RootNode zero-extends it, producing a
   * bad address whose upper 32 bits don't match the executable's load base (crashes
   * in modelInitRwData the first time the player's gait/arm model is initialized,
   * i.e. once gameplay starts past the intro). Assign the real pointer directly;
   * behavior-identical to the N64 assignment, ABI-width fix only. */
  player_gait_object_header.RootNode = &player_gait_hdr;
#else
  player_gait_object_header.RootNode = (int)&player_gait_hdr;
#endif
  return;
}

