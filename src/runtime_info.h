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
#ifndef TRUE_ASYNC_RUNTIME_INFO_H
#define TRUE_ASYNC_RUNTIME_INFO_H

#include "php.h"

/* TrueAsync's runtime information (dev/plans/S9-runtime-info.md): available_parallelism(), loadavg(),
 * CpuSnapshot, cpu_usage() and runtime_stats(), read from the operating system and the scheduler. */

/* Nanosecond counters as CpuSnapshot's properties name them. */
typedef struct
{
	uint64_t wall_ns;
	uint64_t process_user_ns;
	uint64_t process_system_ns;
	uint64_t system_idle_ns;
	uint64_t system_busy_ns;
	uint32_t cpu_count;
	bool has_system_data; /* false: the host's counters could not be read and are 0 */
} async_cpu_snapshot_t;

extern zend_class_entry *async_ce_cpu_snapshot;

void async_register_runtime_info_ce(void);

#endif /* TRUE_ASYNC_RUNTIME_INFO_H */
