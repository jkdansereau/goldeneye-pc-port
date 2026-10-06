#ifndef PORT_FRONTOPTIONS_H
#define PORT_FRONTOPTIONS_H

/*
 * The file-select "PC Options" entry (port/src/frontoptions.c): opens the F10
 * overlay over file select. Hook: front.c constructor_menu05_fileselect calls
 * optionsFileSelectLabel() (#ifdef PORT).
 */

#include <PR/ultratypes.h>
#include <PR/gbi.h>

#ifdef __cplusplus
extern "C" {
#endif


Gfx *optionsFileSelectLabel(Gfx *gdl);   /* file-select hook */

#ifdef __cplusplus
}
#endif

#endif /* PORT_FRONTOPTIONS_H */
