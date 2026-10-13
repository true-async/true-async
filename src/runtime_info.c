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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "Zend/zend_hrtime.h"
#include "php_true_async.h"
#include "runtime_info.h"
#include "runtime_info_arginfo.h"
#include "scheduler.h"

#include <math.h>

#ifdef PHP_WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

#ifdef __linux__
#include <sched.h>
#endif

/* TrueAsync's runtime information (cpu_info.c and async.c:703-775 of ext/async) without libuv: the CPU
 * counters come from the same system calls as TrueAsync's, the parallelism from the affinity mask and the
 * CPU quota read here (libuv's uv_available_parallelism() before 1.49 reads no quota). */

zend_class_entry *async_ce_cpu_snapshot = NULL;

#ifdef __linux__

/* /proc/self/cgroup or /proc/self/mountinfo of RUNTIME_INFO_FILE_MAX - 1 bytes or more is treated as unreadable. */
#define RUNTIME_INFO_FILE_MAX (1024 * 1024)

/* Reads up to `capacity - 1` bytes of `path` into `buffer` and terminates them; -1 when the file cannot be
 * opened or read. Allocates nothing, so a bailout never finds the descriptor open. */
static ssize_t runtime_info_read_into(const char *path, char *buffer, size_t capacity)
{
	const int descriptor = open(path, O_RDONLY | O_CLOEXEC);

	if (UNEXPECTED(descriptor < 0)) {
		return -1;
	}

	size_t filled = 0;

	while (filled < capacity - 1) {
		const ssize_t read_bytes = read(descriptor, buffer + filled, capacity - 1 - filled);

		if (EXPECTED(read_bytes > 0)) {
			filled += (size_t) read_bytes;
		} else if (read_bytes == 0) {
			break;
		} else if (UNEXPECTED(errno != EINTR)) {
			close(descriptor);
			return -1;
		}
	}

	close(descriptor);
	buffer[filled] = '\0';

	return (ssize_t) filled;
}

/* The whole of `path`, NUL-terminated, in an emalloc'd buffer; NULL when it cannot be read or is longer than
 * RUNTIME_INFO_FILE_MAX - 2 bytes. A file that fills the buffer is read again into a larger one. */
static char *runtime_info_read_file(const char *path)
{
	for (size_t capacity = 4096; capacity <= RUNTIME_INFO_FILE_MAX; capacity *= 2) {
		char *buffer = emalloc(capacity);
		const ssize_t filled = runtime_info_read_into(path, buffer, capacity);

		if (UNEXPECTED(filled < 0)) {
			efree(buffer);
			return NULL;
		}

		if (EXPECTED((size_t) filled < capacity - 1)) {
			return buffer;
		}

		efree(buffer);
	}

	return NULL;
}

/* The directory the cgroup and proc paths are read under: empty in production, the fixture of
 * TrueAsync\Test\cpu_limits() in a test. */
static const char *runtime_info_root(void)
{
#ifdef TRUE_ASYNC_TEST_HOOKS
	if (ASYNC_G(test_cpu_root) != NULL) {
		return ZSTR_VAL(ASYNC_G(test_cpu_root));
	}
#endif

	return "";
}

/* Whether the comma-separated `list` holds `item` as a whole entry. */
static bool runtime_info_list_has(const char *list, const char *item)
{
	const size_t item_length = strlen(item);

	while (*list != '\0') {
		const char *comma = strchr(list, ',');
		const size_t entry_length = comma != NULL ? (size_t) (comma - list) : strlen(list);

		if (entry_length == item_length && memcmp(list, item, item_length) == 0) {
			return true;
		}

		if (comma == NULL) {
			break;
		}

		list = comma + 1;
	}

	return false;
}

/* Decodes mountinfo's octal escapes (\040 for a space) in place. */
static void runtime_info_unescape(char *field)
{
	char *target = field;

	for (const char *source = field; *source != '\0'; target++) {
		if (source[0] == '\\' && source[1] >= '0' && source[1] <= '3' && source[2] >= '0' && source[2] <= '7' &&
			source[3] >= '0' && source[3] <= '7') {
			*target = (char) (((source[1] - '0') << 6) | ((source[2] - '0') << 3) | (source[3] - '0'));
			source += 4;
		} else {
			*target = *source++;
		}
	}

	*target = '\0';
}

/* Whether `prefix` is `path` or a directory above it. */
static bool runtime_info_path_under(const char *path, const char *prefix)
{
	const size_t prefix_length = strlen(prefix);

	if (prefix_length == 1 && prefix[0] == '/') {
		return true;
	}

	return strncmp(path, prefix, prefix_length) == 0 && (path[prefix_length] == '\0' || path[prefix_length] == '/');
}

/* The cgroup of this process for the CPU controller, from /proc/self/cgroup, in place in `content`: v1's
 * line whose controllers hold "cpu" when there is one (a hybrid host keeps the controller in v1), otherwise
 * v2's "0::" line. NULL when neither is there. */
static char *runtime_info_cpu_cgroup(char *content, bool *is_v1)
{
	char *v2_path = NULL;

	for (char *line = content; line != NULL && *line != '\0';) {
		char *newline = strchr(line, '\n');

		if (newline != NULL) {
			*newline = '\0';
		}

		char *first_colon = strchr(line, ':');
		char *second_colon = first_colon != NULL ? strchr(first_colon + 1, ':') : NULL;

		if (second_colon != NULL) {
			*first_colon = '\0';
			*second_colon = '\0';

			if (runtime_info_list_has(first_colon + 1, "cpu")) {
				*is_v1 = true;
				return second_colon + 1;
			}

			if (strcmp(line, "0") == 0 && first_colon[1] == '\0') {
				v2_path = second_colon + 1;
			}
		}

		line = newline != NULL ? newline + 1 : NULL;
	}

	*is_v1 = false;

	return v2_path;
}

/* The mount of the CPU controller in /proc/self/mountinfo (`content`, changed in place) whose root is
 * `cgroup_path` or a directory above it, the shortest such root: a mount shows the hierarchy below its root, so
 * that one shows the most levels above `cgroup_path`. Sets `root` and `mount_point`; false when no mount's root
 * is `cgroup_path` or above it. */
static bool runtime_info_cpu_mount(char *content, bool is_v1, const char *cgroup_path, char **root, char **mount_point)
{
	bool found = false;
	size_t best_root_length = 0;

	for (char *line = content; line != NULL && *line != '\0';) {
		char *newline = strchr(line, '\n');

		if (newline != NULL) {
			*newline = '\0';
		}

		/* id parent major:minor root mount-point options [optional...] - type source super-options */
		char *fields[16];
		uint32_t field_count = 0;
		char *tokenizer_state = NULL;

		for (char *field = strtok_r(line, " ", &tokenizer_state); field != NULL && field_count < 16;
			 field = strtok_r(NULL, " ", &tokenizer_state)) {
			fields[field_count++] = field;
		}

		uint32_t separator = 6;

		while (separator < field_count && strcmp(fields[separator], "-") != 0) {
			separator++;
		}

		if (separator + 3 < field_count) {
			const char *type = fields[separator + 1];
			const bool matches = is_v1
					? strcmp(type, "cgroup") == 0 && runtime_info_list_has(fields[separator + 3], "cpu")
					: strcmp(type, "cgroup2") == 0;

			if (matches) {
				runtime_info_unescape(fields[3]);
				runtime_info_unescape(fields[4]);

				const size_t root_length = strlen(fields[3]);

				if (runtime_info_path_under(cgroup_path, fields[3]) && (!found || root_length < best_root_length)) {
					*root = fields[3];
					*mount_point = fields[4];
					best_root_length = root_length;
					found = true;
				}
			}
		}

		line = newline != NULL ? newline + 1 : NULL;
	}

	return found;
}

/* Parses the decimal number at the start of `text`, which ends at a space, a newline or the string's end;
 * false on anything else. */
static bool runtime_info_parse_number(const char *text, uint64_t *number)
{
	if (*text < '0' || *text > '9') {
		return false;
	}

	char *end = NULL;
	errno = 0;
	const unsigned long long parsed = strtoull(text, &end, 10);

	if (UNEXPECTED(errno == ERANGE || (*end != '\0' && *end != ' ' && *end != '\n'))) {
		return false;
	}

	*number = (uint64_t) parsed;

	return true;
}

/* Reads a cgroup file of a few numbers whole into `buffer`; false when it cannot be read or fills `buffer`, since
 * a longer file cut there would parse as another number. */
static bool runtime_info_read_small(const char *path, char *buffer, size_t capacity)
{
	const ssize_t filled = runtime_info_read_into(path, buffer, capacity);

	return filled >= 0 && (size_t) filled < capacity - 1;
}

/* The quota of the cgroup directory `directory` in CPUs; 0 for none or for a file that cannot be read. */
static double runtime_info_directory_quota(const char *directory, bool is_v1)
{
	char path[MAXPATHLEN];
	char content[64];
	uint64_t quota = 0;
	uint64_t period = 0;

	if (is_v1) {
		if (snprintf(path, sizeof(path), "%s/cpu.cfs_quota_us", directory) >= (int) sizeof(path) ||
			!runtime_info_read_small(path, content, sizeof(content)) || !runtime_info_parse_number(content, &quota)) {
			/* -1 is no quota; it fails the parse as an unreadable file does. */
			return 0;
		}

		if (snprintf(path, sizeof(path), "%s/cpu.cfs_period_us", directory) >= (int) sizeof(path) ||
			!runtime_info_read_small(path, content, sizeof(content)) || !runtime_info_parse_number(content, &period)) {
			return 0;
		}
	} else {
		if (snprintf(path, sizeof(path), "%s/cpu.max", directory) >= (int) sizeof(path) ||
			!runtime_info_read_small(path, content, sizeof(content))) {
			return 0;
		}

		/* "quota period"; "max period" is no quota and fails the parse */
		const char *space = strchr(content, ' ');

		if (space == NULL || !runtime_info_parse_number(content, &quota) ||
			!runtime_info_parse_number(space + 1, &period)) {
			return 0;
		}
	}

	if (UNEXPECTED(quota == 0 || period == 0)) {
		return 0;
	}

	return (double) quota / (double) period;
}

/* The smallest CPU quota from the process's cgroup up to its controller's mount point, in CPUs; 0 for none. */
static double runtime_info_cpu_quota(void)
{
	const char *root_directory = runtime_info_root();
	char path[MAXPATHLEN];
	double smallest_quota = 0;

	if (UNEXPECTED(snprintf(path, sizeof(path), "%s/proc/self/cgroup", root_directory) >= (int) sizeof(path))) {
		return 0;
	}

	char *cgroups = runtime_info_read_file(path);

	if (UNEXPECTED(cgroups == NULL)) {
		return 0;
	}

	if (UNEXPECTED(snprintf(path, sizeof(path), "%s/proc/self/mountinfo", root_directory) >= (int) sizeof(path))) {
		efree(cgroups);
		return 0;
	}

	char *mounts = runtime_info_read_file(path);

	if (UNEXPECTED(mounts == NULL)) {
		efree(cgroups);
		return 0;
	}

	bool is_v1 = false;
	const char *cgroup_path = runtime_info_cpu_cgroup(cgroups, &is_v1);
	char *mount_root = NULL;
	char *mount_point = NULL;

	/* A path that climbs with ".." is a cgroup outside this cgroup namespace's root. runtime_info_path_under()
	 * puts it under mount root "/", but that mount shows the namespace's root, a sibling of it; a mount from
	 * above the namespace, whose root also climbs with "..", is rare and not read. */
	if (cgroup_path != NULL && !runtime_info_path_under(cgroup_path, "/..") &&
		runtime_info_cpu_mount(mounts, is_v1, cgroup_path, &mount_root, &mount_point)) {
		/* The cgroup's directory below the mount point: the path without the mount's root. */
		const char *relative = strcmp(mount_root, "/") == 0 ? cgroup_path : cgroup_path + strlen(mount_root);

		const int base_length = snprintf(path, sizeof(path), "%s%s", root_directory, mount_point);
		const int full_length = snprintf(path, sizeof(path), "%s%s%s", root_directory, mount_point, relative);

		if (EXPECTED(full_length < (int) sizeof(path))) {
			/* The root cgroup "/" ends the path with a slash; dropped so the mount point is not read twice. */
			if (full_length > base_length && path[full_length - 1] == '/') {
				path[full_length - 1] = '\0';
			}

			while (true) {
				const double quota = runtime_info_directory_quota(path, is_v1);

				if (quota > 0 && (smallest_quota == 0 || quota < smallest_quota)) {
					smallest_quota = quota;
				}

				char *slash = strrchr(path + base_length, '/');

				if (slash == NULL) {
					break;
				}

				*slash = '\0';
			}
		}
	}

	efree(mounts);
	efree(cgroups);

	return smallest_quota;
}

static uint32_t runtime_info_affinity_count(void)
{
#ifdef TRUE_ASYNC_TEST_HOOKS
	if (ASYNC_G(test_cpu_affinity) > 0) {
		return (uint32_t) ASYNC_G(test_cpu_affinity);
	}
#endif

	cpu_set_t cpus;

	if (EXPECTED(sched_getaffinity(0, sizeof(cpus), &cpus) == 0)) {
		const int count = CPU_COUNT(&cpus);

		if (EXPECTED(count > 0)) {
			return (uint32_t) count;
		}
	}

	const long online = sysconf(_SC_NPROCESSORS_ONLN);

	return online > 0 ? (uint32_t) online : 1;
}

#elif defined(PHP_WIN32)

static uint32_t runtime_info_mask_count(ULONG_PTR mask)
{
	uint32_t count = 0;

	for (; mask != 0; mask &= mask - 1) {
		count++;
	}

	return count;
}

/* The CPUs the process may run on. GetProcessAffinityMask() gives the mask of the calling thread's processor
 * group; before Windows 11 it gives 0 for a process whose threads were placed in several groups, and then each
 * group the process runs in counts with all its CPUs, since Windows keeps no per-group process mask. */
static uint32_t runtime_info_affinity_count(void)
{
	DWORD_PTR process_mask = 0;
	DWORD_PTR system_mask = 0;

	if (EXPECTED(GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) && process_mask != 0)) {
		/* From Windows 11 and Server 2022 (build 20348) a process spans every group unless its affinity was set,
		 * while the masks still cover one group. No call tells that default from an affinity set to the whole
		 * group, so a whole-group mask counts every group: such an affinity is over-counted. */
		if (process_mask == system_mask && EG(windows_version_info).dwBuildNumber >= 20348 &&
			GetActiveProcessorGroupCount() > 1) {
			return GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
		}

		return runtime_info_mask_count(process_mask);
	}

	USHORT groups[64];
	USHORT group_count = (USHORT) (sizeof(groups) / sizeof(groups[0]));
	uint32_t count = 0;

	if (GetProcessGroupAffinity(GetCurrentProcess(), &group_count, groups)) {
		for (USHORT i = 0; i < group_count; i++) {
			count += GetActiveProcessorCount(groups[i]);
		}
	}

	if (UNEXPECTED(count == 0)) {
		count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
	}

	return count > 0 ? count : 1;
}

/* The hard cap of the job object the process runs in (a container started with --cpus), in CPUs; 0 for none.
 * CpuRate and MaxRate are in 1/100 of a percent of every CPU of the host. Only the innermost job is read: the
 * API gives no handle to the job around it. */
static double runtime_info_cpu_quota(void)
{
	JOBOBJECT_CPU_RATE_CONTROL_INFORMATION rate_control;
	DWORD rate = 0;

	if (!QueryInformationJobObject(
				NULL, JobObjectCpuRateControlInformation, &rate_control, sizeof(rate_control), NULL) ||
		!(rate_control.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_ENABLE)) {
		return 0;
	}

#ifdef JOB_OBJECT_CPU_RATE_CONTROL_MIN_MAX_RATE
	if (rate_control.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_MIN_MAX_RATE) {
		rate = rate_control.MaxRate;
	}
#endif

	if ((rate_control.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP) &&
		!(rate_control.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_WEIGHT_BASED)) {
		rate = rate_control.CpuRate;
	}

	return (double) rate / 10000.0 * (double) GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
}

#else

static uint32_t runtime_info_affinity_count(void)
{
	const long online = sysconf(_SC_NPROCESSORS_ONLN);

	return online > 0 ? (uint32_t) online : 1;
}

static double runtime_info_cpu_quota(void)
{
	return 0;
}

#endif

/* The CPUs the process may use, unrounded: the affinity count lowered to the CPU quota. */
static double runtime_info_usable_cpus(void)
{
	const double affinity_count = (double) runtime_info_affinity_count();
	const double quota = runtime_info_cpu_quota();

	return quota > 0 && quota < affinity_count ? quota : affinity_count;
}

static uint32_t runtime_info_online_cpus(void)
{
#ifdef PHP_WIN32
	const DWORD online = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
#else
	const long online = sysconf(_SC_NPROCESSORS_ONLN);
#endif

	return online > 0 ? (uint32_t) online : 1;
}

#ifdef PHP_WIN32

/* FILETIME counts 100-nanosecond intervals. */
static uint64_t runtime_info_filetime_ns(FILETIME filetime)
{
	ULARGE_INTEGER value;

	value.LowPart = filetime.dwLowDateTime;
	value.HighPart = filetime.dwHighDateTime;

	return value.QuadPart * 100;
}

static void runtime_info_process_cpu(async_cpu_snapshot_t *snapshot)
{
	FILETIME creation_time;
	FILETIME exit_time;
	FILETIME kernel;
	FILETIME user;

	if (EXPECTED(GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time, &kernel, &user))) {
		snapshot->process_user_ns = runtime_info_filetime_ns(user);
		snapshot->process_system_ns = runtime_info_filetime_ns(kernel);
	}
}

static void runtime_info_system_cpu(async_cpu_snapshot_t *snapshot)
{
	FILETIME idle;
	FILETIME kernel;
	FILETIME user;

	if (UNEXPECTED(!GetSystemTimes(&idle, &kernel, &user))) {
		return;
	}

	const uint64_t idle_ns = runtime_info_filetime_ns(idle);
	/* The kernel time includes the idle time. */
	const uint64_t total_ns = runtime_info_filetime_ns(kernel) + runtime_info_filetime_ns(user);

	snapshot->system_idle_ns = idle_ns;
	snapshot->system_busy_ns = total_ns > idle_ns ? total_ns - idle_ns : 0;
	snapshot->has_system_data = true;
}

#else

static void runtime_info_process_cpu(async_cpu_snapshot_t *snapshot)
{
	struct rusage usage;

	if (EXPECTED(getrusage(RUSAGE_SELF, &usage) == 0)) {
		snapshot->process_user_ns =
				(uint64_t) usage.ru_utime.tv_sec * 1000000000 + (uint64_t) usage.ru_utime.tv_usec * 1000;
		snapshot->process_system_ns =
				(uint64_t) usage.ru_stime.tv_sec * 1000000000 + (uint64_t) usage.ru_stime.tv_usec * 1000;
	}
}

#ifdef __linux__
/* Exact in integers, so a host's counters past 2^64 ns wrap as unsigned numbers do and deltas stay right; a double
 * that large cast to uint64_t is undefined. */
static uint64_t runtime_info_ticks_to_ns(uint64_t ticks, uint64_t ticks_per_second)
{
	return ticks / ticks_per_second * 1000000000 + ticks % ticks_per_second * 1000000000 / ticks_per_second;
}
#endif

static void runtime_info_system_cpu(async_cpu_snapshot_t *snapshot)
{
#ifdef __linux__
	char line[512];

	if (UNEXPECTED(runtime_info_read_into("/proc/stat", line, sizeof(line)) < 0)) {
		return;
	}

	/* "cpu  user nice system idle iowait irq softirq steal guest guest_nice" in clock ticks; the kernel
	 * already counts guest and guest_nice in user and nice. */
	unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;

	if (UNEXPECTED(sscanf(line,
						  "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
						  &user,
						  &nice,
						  &system,
						  &idle,
						  &iowait,
						  &irq,
						  &softirq,
						  &steal) < 4)) {
		return;
	}

	long ticks_per_second = sysconf(_SC_CLK_TCK);

	if (UNEXPECTED(ticks_per_second <= 0)) {
		ticks_per_second = 100;
	}

	snapshot->system_idle_ns = runtime_info_ticks_to_ns(idle + iowait, (uint64_t) ticks_per_second);
	snapshot->system_busy_ns =
			runtime_info_ticks_to_ns(user + nice + system + irq + softirq + steal, (uint64_t) ticks_per_second);
	snapshot->has_system_data = true;
#else
	(void) snapshot;
#endif
}

#endif

static void runtime_info_take_snapshot(async_cpu_snapshot_t *snapshot)
{
	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->wall_ns = zend_hrtime();
	runtime_info_process_cpu(snapshot);
	runtime_info_system_cpu(snapshot);
	snapshot->cpu_count = runtime_info_online_cpus();
}

/* [1min, 5min, 15min] into `return_value`, or null where there is no load average. */
static void runtime_info_loadavg(zval *return_value)
{
#ifdef HAVE_GETLOADAVG
	double averages[3];

	if (EXPECTED(getloadavg(averages, 3) == 3)) {
		array_init_size(return_value, 3);
		add_next_index_double(return_value, averages[0]);
		add_next_index_double(return_value, averages[1]);
		add_next_index_double(return_value, averages[2]);
		return;
	}
#endif

	ZVAL_NULL(return_value);
}

ZEND_FUNCTION(Async_available_parallelism)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const uint32_t affinity_count = runtime_info_affinity_count();
	const double quota = runtime_info_cpu_quota();
	uint32_t parallelism = affinity_count;

	if (quota > 0 && quota < (double) affinity_count) {
		parallelism = (uint32_t) ceil(quota);
	}

	RETURN_LONG(parallelism > 0 ? parallelism : 1);
}

ZEND_FUNCTION(Async_loadavg)
{
	ZEND_PARSE_PARAMETERS_NONE();

	runtime_info_loadavg(return_value);
}

ZEND_FUNCTION(Async_cpu_usage)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_cpu_snapshot_t current;
	runtime_info_take_snapshot(&current);

	const async_cpu_snapshot_t previous = ASYNC_G(cpu_usage_last);
	const bool first_call = !ASYNC_G(cpu_usage_started);

	ASYNC_G(cpu_usage_last) = current;
	ASYNC_G(cpu_usage_started) = true;

	double interval_sec = 0;
	double process_cores = 0;
	double process_percent = 0;
	double system_percent = 0;

	if (!first_call && current.wall_ns > previous.wall_ns) {
		const uint64_t wall_ns = current.wall_ns - previous.wall_ns;
		const uint64_t previous_process_ns = previous.process_user_ns + previous.process_system_ns;
		const uint64_t current_process_ns = current.process_user_ns + current.process_system_ns;
		const uint64_t process_ns =
				current_process_ns > previous_process_ns ? current_process_ns - previous_process_ns : 0;

		interval_sec = (double) wall_ns / 1e9;
		process_cores = (double) process_ns / (double) wall_ns;
		/* A share of the CPUs the process may use (Edmond, DECISIONS S9.48); a quota lets threads run past it
		 * for part of its period, so the share is capped. */
		process_percent = MIN(process_cores / runtime_info_usable_cpus() * 100.0, 100.0);

		if (current.has_system_data && previous.has_system_data) {
			const uint64_t idle_ns = current.system_idle_ns > previous.system_idle_ns
					? current.system_idle_ns - previous.system_idle_ns
					: 0;
			const uint64_t busy_ns = current.system_busy_ns > previous.system_busy_ns
					? current.system_busy_ns - previous.system_busy_ns
					: 0;

			if (idle_ns + busy_ns > 0) {
				system_percent = (double) busy_ns / (double) (idle_ns + busy_ns) * 100.0;
			}
		}
	}

	zval loadavg;
	runtime_info_loadavg(&loadavg);

	array_init_size(return_value, 6);
	add_assoc_double(return_value, "process_cores", process_cores);
	add_assoc_double(return_value, "process_percent", process_percent);
	add_assoc_double(return_value, "system_percent", system_percent);
	add_assoc_long(return_value, "cpu_count", (zend_long) current.cpu_count);
	add_assoc_double(return_value, "interval_sec", interval_sec);
	add_assoc_zval(return_value, "loadavg", &loadavg);
}

ZEND_FUNCTION(Async_runtime_stats)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const circular_buffer_t *const pool = &ASYNC_G(fiber_context_pool);
	const size_t pool_count = circular_buffer_count(pool);
	/* One slot of the ring stays free (circular_buffer.h); TrueAsync reports the usable ones. */
	const size_t pool_capacity = pool->capacity > 0 ? pool->capacity - 1 : 0;
	/* The size the scheduler gives a new context (scheduler.c, fiber_context_take()); a pooled one keeps the
	 * fiber.stack_size it was made with. */
	const size_t context_size = EG(fiber_stack_size) + ZEND_FIBER_VM_STACK_SIZE;

	array_init_size(return_value, 10);
	add_assoc_long(return_value, "coroutines_total", (zend_long) zend_hash_num_elements(&ASYNC_G(coroutines)));
	add_assoc_long(return_value, "coroutines_active", (zend_long) ZEND_ASYNC_GET_COROUTINE_COUNT());
	add_assoc_long(return_value, "microtasks_queue", (zend_long) circular_buffer_count(&ASYNC_G(microtasks)));
	add_assoc_long(return_value, "coroutine_queue", (zend_long) circular_buffer_count(&ASYNC_G(run_queue)));
	/* The scheduler cleans a coroutine's wait when it wakes and keeps no list of woken coroutines. */
	add_assoc_long(return_value, "resumed_queue", 0);
	add_assoc_long(return_value, "fiber_pool_count", (zend_long) pool_count);
	add_assoc_long(return_value, "fiber_pool_capacity", (zend_long) pool_capacity);
	add_assoc_long(return_value, "fiber_pool_min", (zend_long) ASYNC_FIBER_POOL_SIZE);
	add_assoc_long(return_value, "fiber_stack_size", (zend_long) EG(fiber_stack_size));
	add_assoc_long(return_value, "fiber_pool_virtual_bytes", (zend_long) (pool_count * context_size));
}

ZEND_METHOD(Async_CpuSnapshot, __construct)
{
	ZEND_PARSE_PARAMETERS_NONE();
}

ZEND_METHOD(Async_CpuSnapshot, now)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_cpu_snapshot_t snapshot;
	runtime_info_take_snapshot(&snapshot);

	object_init_ex(return_value, async_ce_cpu_snapshot);

	zend_object *object = Z_OBJ_P(return_value);

	/* The properties are readonly: set from the class's own scope, as TrueAsync does (cpu_info.c:231). */
	zend_update_property_long(async_ce_cpu_snapshot, object, ZEND_STRL("wallNs"), (zend_long) snapshot.wall_ns);
	zend_update_property_long(
			async_ce_cpu_snapshot, object, ZEND_STRL("processUserNs"), (zend_long) snapshot.process_user_ns);
	zend_update_property_long(
			async_ce_cpu_snapshot, object, ZEND_STRL("processSystemNs"), (zend_long) snapshot.process_system_ns);
	zend_update_property_long(
			async_ce_cpu_snapshot, object, ZEND_STRL("systemIdleNs"), (zend_long) snapshot.system_idle_ns);
	zend_update_property_long(
			async_ce_cpu_snapshot, object, ZEND_STRL("systemBusyNs"), (zend_long) snapshot.system_busy_ns);
	zend_update_property_long(async_ce_cpu_snapshot, object, ZEND_STRL("cpuCount"), (zend_long) snapshot.cpu_count);
}

void async_register_runtime_info_ce(void)
{
	async_ce_cpu_snapshot = register_class_Async_CpuSnapshot();
}
