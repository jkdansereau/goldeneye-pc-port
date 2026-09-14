#ifndef PORT_ROMCONVERT_H
#define PORT_ROMCONVERT_H

/*
 * First-run sidecar conversion — see port/src/romconvert.c. Called from
 * romdataInit() once a valid ROM image is in hand; spawns the bundled frozen
 * converter (ge007-convert) if the pcmodels/pccg sidecars are missing.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* romImg: mapped/read ROM image (country byte at 0x3E selects the region);
 * romRelPath: the "$S/..." / "$E/..." / "./..." path the ROM was opened from.
 * Returns 1 when both sidecars are present, -1 when they could not be
 * produced (caller should abort boot). */
int romConvertEnsureSidecars(const unsigned char *romImg, const char *romRelPath);

#ifdef __cplusplus
}
#endif

#endif /* PORT_ROMCONVERT_H */
