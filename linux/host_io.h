/* Linux host policies around upstream pk_savevf; no game-specific addresses. */
#ifndef POCKET_HOST_IO_H
#define POCKET_HOST_IO_H
#include <mgba-util/vfs.h>
struct pocket_save_status {
  bool failed, closed;
};
/* Takes inner on success or failure. status must outlive the returned VFile. */
struct VFile *pocket_save_wrap(struct VFile *inner,
                               struct pocket_save_status *status);
#endif
