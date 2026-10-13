/*
   +----------------------------------------------------------------------+
   | Copyright © TrueAsync contributors.                                  |
   +----------------------------------------------------------------------+
   | This source file is subject to the Modified BSD License that is      |
   | bundled with this package in the file LICENSE.                       |
   |                                                                      |
   | SPDX-License-Identifier: BSD-3-Clause                                |
   +----------------------------------------------------------------------+
   | Authors: Edmond <edmondifthen@proton.me>                             |
   +----------------------------------------------------------------------+
*/
#ifndef TRUE_ASYNC_FS_WATCHER_H
#define TRUE_ASYNC_FS_WATCHER_H

#include "php.h"
#include "true_async_API.h"

/* Async\FileSystemWatcher (dev/plans/S9-fs-watcher.md): TrueAsync's watcher on a per-request inotify
 * descriptor that the reactor polls; Linux only. */

typedef struct _async_fs_registry_s async_fs_registry_t;

extern zend_class_entry *async_ce_fs_watcher;
extern zend_class_entry *async_ce_filesystem_event;

void async_register_fs_watcher_ce(void);

/* Closes every watcher and the inotify descriptor: called after the scheduler's shutdown and before the
 * reactor's, which asserts that its own ops are gone. A watcher freed later finds no registry. */
void async_fs_watcher_request_shutdown(void);

/* A forked child's rebuild, before the reactor resubmits its own ops: a new inotify descriptor with every
 * watch added again, and the registry's POLL op re-armed on it and left on `own` for that resubmit. */
void async_fs_watcher_rebuild(void);

#endif /* TRUE_ASYNC_FS_WATCHER_H */
