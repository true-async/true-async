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
#include "fuzz.h"

#include <stdlib.h>
#include <string.h>

/* A seed in decimal or 0x hex; 0 for a malformed one. */
static uint64_t parse_seed(const char *text)
{
	const int base = (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) ? 16 : 10;

	return (uint64_t) strtoull(text, NULL, base);
}

void async_fuzz_init(async_fuzz_state_t *state)
{
	const char *scheduler_mode = getenv("TRUE_ASYNC_SCHED");

	state->mode = ASYNC_FUZZ_MODE_FIFO;
	state->rng_state = 0;

	if (scheduler_mode == NULL || strncmp(scheduler_mode, "random", sizeof("random") - 1) != 0) {
		return;
	}

	const char *colon = strchr(scheduler_mode, ':');
	const uint64_t seed = colon != NULL ? parse_seed(colon + 1) : 0;

	state->mode = ASYNC_FUZZ_MODE_RANDOM;
	/* TrueAsync's mix: a seed gives the same random stream in both extensions. */
	state->rng_state = seed ^ 0xA5A5A5A5DEADBEEFULL;
}
