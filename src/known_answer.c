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

/* Two identical functions planted for the mutation tool's known-answer check
 * (dev/plans/S2.md, section 6): tools/mull/known-answer.phpt calls the first and never the
 * second, so a working tool kills the first one's mutants and leaves the second one's alive.
 * Every mutant of the body changes the result for one of the test's inputs; an equivalent mutant
 * (`<` to `<=` in a clamp, say) would survive any test and break the check.
 * Built only with --enable-true-async-known-answer. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "known_answer.h"

static zend_long known_answer_classify(zend_long value, zend_long limit)
{
	if (value < limit) {
		return value + limit;
	}

	return value - limit;
}

static zend_long known_answer_classify_untested(zend_long value, zend_long limit)
{
	if (value < limit) {
		return value + limit;
	}

	return value - limit;
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_known_answer, 0, 2, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, value, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, limit, IS_LONG, 0)
ZEND_END_ARG_INFO()

static ZEND_FUNCTION(true_async_known_answer_tested)
{
	zend_long value, limit;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_LONG(value)
		Z_PARAM_LONG(limit)
	ZEND_PARSE_PARAMETERS_END();

	RETURN_LONG(known_answer_classify(value, limit));
}

static ZEND_FUNCTION(true_async_known_answer_untested)
{
	zend_long value, limit;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_LONG(value)
		Z_PARAM_LONG(limit)
	ZEND_PARSE_PARAMETERS_END();

	RETURN_LONG(known_answer_classify_untested(value, limit));
}

/* clang-format off */
const zend_function_entry true_async_known_answer_functions[] = {
	ZEND_FE(true_async_known_answer_tested, arginfo_known_answer)
	ZEND_FE(true_async_known_answer_untested, arginfo_known_answer)
	ZEND_FE_END
};
/* clang-format on */
