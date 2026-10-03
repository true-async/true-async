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
#ifndef TRUE_ASYNC_FUZZ_H
#define TRUE_ASYNC_FUZZ_H

/* The scheduler's fuzz hook, TrueAsync's internal/fuzz.h (built with --enable-true-async-fuzz): before
 * each pop the run queue's head may swap places with another queued coroutine, so one test run with
 * many seeds goes through many interleavings. TRUE_ASYNC_SCHED picks the mode at request start:
 * "fifo" (unset or unknown: the order of a normal build) or "random:<seed>", decimal or 0x hex; the
 * same seed gives the same order. */

#include <stdint.h>

typedef enum
{
	ASYNC_FUZZ_MODE_FIFO = 0,
	ASYNC_FUZZ_MODE_RANDOM = 1,
} async_fuzz_mode_t;

typedef struct
{
	async_fuzz_mode_t mode;
	uint64_t rng_state;
} async_fuzz_state_t;

/* Reads TRUE_ASYNC_SCHED into `state`; every request starts from the seed again. */
void async_fuzz_init(async_fuzz_state_t *state);

/* SplitMix64: fast, statistically decent, no dependencies. */
static inline uint64_t async_fuzz_next_u64(async_fuzz_state_t *state)
{
	uint64_t z = (state->rng_state += 0x9E3779B97F4A7C15ULL);
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;

	return z ^ (z >> 31);
}

/* The queue position in [0, count) to swap with the head before the next pop; 0 swaps nothing. In
 * random mode half the pops keep the head (TrueAsync's coin): some FIFO bias, so the order still
 * makes progress. */
static inline uint32_t async_fuzz_scheduler_pick(async_fuzz_state_t *state, const uint32_t count)
{
	if (state->mode == ASYNC_FUZZ_MODE_FIFO || count <= 1) {
		return 0;
	}

	if ((async_fuzz_next_u64(state) & 1ULL) == 0) {
		return 0;
	}

	return (uint32_t) (async_fuzz_next_u64(state) % count);
}

#endif /* TRUE_ASYNC_FUZZ_H */
