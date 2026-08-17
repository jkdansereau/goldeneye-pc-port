/*
 * Filesystem abstraction over the ROM + data dir.
 *
 * A small POSIX-ish file API so the port layer has a uniform interface across
 * Windows/Linux/macOS.
 *
 * Modelled on the PD port's port/src/fs.c (~295 lines).
 *
 * STATUS: scaffolding stub — implement during Phase 1.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "fs.h"

struct FSFile {
    FILE *f;
};

FSFile *fsOpen(const char *path, const char *mode)
{
    FSFile *f = (FSFile *)calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->f = fopen(path, mode);
    if (!f->f) { free(f); return NULL; }
    return f;
}

void fsClose(FSFile *f)
{
    if (f) { if (f->f) fclose(f->f); free(f); }
}

int32_t fsRead(FSFile *f, void *buf, int32_t size)
{
    if (!f || !f->f) return -1;
    return (int32_t)fread(buf, 1, (size_t)size, f->f);
}

int32_t fsWrite(FSFile *f, const void *buf, int32_t size)
{
    if (!f || !f->f) return -1;
    return (int32_t)fwrite(buf, 1, (size_t)size, f->f);
}

int fsSeek(FSFile *f, int32_t offset, int whence)
{
    if (!f || !f->f) return -1;
    return fseek(f->f, (long)offset, whence);
}

int32_t fsTell(FSFile *f)
{
    if (!f || !f->f) return -1;
    return (int32_t)ftell(f->f);
}

int32_t fsSize(FSFile *f)
{
    if (!f || !f->f) return -1;
    int32_t cur = ftell(f->f);
    fseek(f->f, 0, SEEK_END);
    int32_t sz = ftell(f->f);
    fseek(f->f, cur, SEEK_SET);
    return sz;
}

int fsExists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}
