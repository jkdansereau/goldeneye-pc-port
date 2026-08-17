#ifndef PORT_FS_H
#define PORT_FS_H

/*
 * Filesystem abstraction over the ROM + data dir.
 * Modelled on the PD port's port/include/fs.h.
 *
 * Provides a small POSIX-ish file API so the port layer (and any game code
 * that touches files) has a uniform interface across Windows/Linux/macOS.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FS_MAXPATH 512

typedef struct FSFile FSFile;

/* Open a file. mode is a fopen-style string ("rb", "wb", "r+b", ...). */
FSFile *fsOpen(const char *path, const char *mode);
void    fsClose(FSFile *f);

/* Read up to size bytes into buf; returns bytes read (-1 on error/EOF). */
int32_t fsRead(FSFile *f, void *buf, int32_t size);
/* Write size bytes from buf; returns bytes written (-1 on error). */
int32_t fsWrite(FSFile *f, const void *buf, int32_t size);

/* Seek to offset (whence: SEEK_SET/CUR/END). Returns 0 on success. */
int     fsSeek(FSFile *f, int32_t offset, int whence);
/* Current file position. */
int32_t fsTell(FSFile *f);
/* File size in bytes. */
int32_t fsSize(FSFile *f);

/* Does the file exist? */
int     fsExists(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* PORT_FS_H */
