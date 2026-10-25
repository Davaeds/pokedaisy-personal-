/* GPL-3.0. Keep upstream pk_savevf.c unchanged; enforce Pocket's error policy
 * around its underlying file. Shared by GB/GBC/GBA, independent of companion.
 */
#include "host_io.h"
#include "../app/src/main/cpp/pk_savevf.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
struct checked_save {
  struct VFile d;
  struct VFile *inner;
  struct pocket_save_status *status;
};
static bool close_save(struct VFile *vf) {
  struct checked_save *s = (void *)vf;
  bool ok = s->inner->close(s->inner);
  s->status->failed |= !ok;
  s->status->closed = true;
  free(s);
  return ok;
}
static off_t seek_save(struct VFile *vf, off_t at, int whence) {
  struct checked_save *s = (void *)vf;
  off_t n = s->inner->seek(s->inner, at, whence);
  if (n < 0)
    s->status->failed = true;
  return n;
}
static ssize_t read_save(struct VFile *vf, void *out, size_t size) {
  struct checked_save *s = (void *)vf;
  if (s->status->failed)
    return -1;
  off_t pos = s->inner->seek(s->inner, 0, SEEK_CUR);
  ssize_t total = s->inner->size(s->inner);
  ssize_t n = s->inner->read(s->inner, out, size);
  if (pos < 0 || total < 0 || n < 0 ||
      (pos < total &&
       (size_t)n <
           ((size_t)(total - pos) < size ? (size_t)(total - pos) : size)))
    s->status->failed = true;
  return n;
}
static ssize_t line_save(struct VFile *vf, char *out, size_t size) {
  struct checked_save *s = (void *)vf;
  ssize_t n = s->inner->readline(s->inner, out, size);
  if (n < 0)
    s->status->failed = true;
  return n;
}
static ssize_t write_save(struct VFile *vf, const void *data, size_t size) {
  struct checked_save *s = (void *)vf;
  if (s->status->failed)
    return -1;
  ssize_t n = s->inner->write(s->inner, data, size);
  if (n < 0 || (size_t)n != size)
    s->status->failed = true;
  return n;
}
static void *map_save(struct VFile *vf, size_t size, int flags) {
  struct checked_save *s = (void *)vf;
  return s->inner->map(s->inner, size, flags);
}
static void unmap_save(struct VFile *vf, void *p, size_t size) {
  struct checked_save *s = (void *)vf;
  s->inner->unmap(s->inner, p, size);
}
static void truncate_save(struct VFile *vf, size_t size) {
  struct checked_save *s = (void *)vf;
  if (s->status->failed)
    return;
  s->inner->truncate(s->inner, size);
  ssize_t actual = s->inner->size(s->inner);
  if (actual < 0 || (size_t)actual != size)
    s->status->failed = true;
}
static ssize_t size_save(struct VFile *vf) {
  struct checked_save *s = (void *)vf;
  ssize_t size = s->inner->size(s->inner);
  if (size < 0)
    s->status->failed = true;
  return size;
}
static bool sync_save(struct VFile *vf, void *p, size_t size) {
  struct checked_save *s = (void *)vf;
  if (s->status->failed)
    return false;
  bool ok = s->inner->sync(s->inner, p, size);
  if (!ok)
    s->status->failed = true;
  return ok;
}
struct VFile *pocket_save_wrap(struct VFile *inner,
                               struct pocket_save_status *status) {
  memset(status, 0, sizeof *status);
  if (!inner) {
    status->failed = true;
    status->closed = true;
    return NULL;
  }
  struct checked_save *s = calloc(1, sizeof *s);
  if (!s) {
    inner->close(inner);
    status->closed = true;
    status->failed = true;
    return NULL;
  }
  s->inner = inner;
  s->status = status;
  s->d = (struct VFile){.close = close_save,
                        .seek = seek_save,
                        .read = read_save,
                        .readline = line_save,
                        .write = write_save,
                        .map = map_save,
                        .unmap = unmap_save,
                        .truncate = truncate_save,
                        .size = size_save,
                        .sync = sync_save};
  struct VFile *result = pk_save_vf(&s->d);
  if (result == &s->d) {
    close_save(result);
    status->failed = true;
    return NULL;
  }
  return result;
}
