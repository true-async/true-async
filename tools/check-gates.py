#!/usr/bin/env python3
"""Grep gates over the extension sources (dev/plans/S3.md, section 11).

    check-gates.py

Each gate names a construct the port must not carry (a fork construct, or a call a principle
forbids), or a build flag that must stay. Prints every violation with file:line; exits 1 if there
is one. The gate "every `flags =` on an event contains ASYNC_AWAITABLE_F_EVENT" waits for the first
event (S4).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# The zend_async_*_t names the RFC core declares (Zend/zend_async_API.h, zend_globals.h at the
# pinned core); any other is a fork type, renamed async_* by D12. A core update that adds a type
# adds it here.
CORE_TYPES = {
    'zend_async_call_on_main_stack_t', 'zend_async_cancel_t', 'zend_async_context_t',
    'zend_async_coroutine_add_awaiting_info_t', 'zend_async_coroutine_add_finish_handler_t',
    'zend_async_coroutine_add_switch_handler_t', 'zend_async_coroutine_await_t',
    'zend_async_coroutine_execute_data_t', 'zend_async_coroutine_from_object_t',
    'zend_async_coroutine_get_awaiting_info_t', 'zend_async_coroutine_remove_awaiting_info_t',
    'zend_async_coroutine_remove_finish_handler_t',
    'zend_async_coroutine_remove_switch_handler_t', 'zend_async_defer_t',
    'zend_async_enqueue_coroutine_t', 'zend_async_gc_new_coroutine_t',
    'zend_async_get_class_ce_t', 'zend_async_get_coroutine_count_t', 'zend_async_globals_t',
    'zend_async_intercept_fiber_t',
    'zend_async_microtask_t', 'zend_async_new_context_t', 'zend_async_new_coroutine_t',
    'zend_async_scheduler_api_t', 'zend_async_scheduler_launch_t', 'zend_async_shutdown_t',
    'zend_async_state_t', 'zend_async_suspend_t'
}

# (what is forbidden, pattern) over src/ and php_true_async.h.
FORBIDDEN = [
    # A scope's own event (src/scope.h) is not one.
    ('the event embedded in a coroutine', r'(?<!scope)(?<!scope\))->event\.'),
    ('a fork event macro', r'\bZEND_ASYNC_EVENT_\w+'),
    ('the fork object-to-event cast', r'\bZEND_ASYNC_OBJECT_TO_EVENT\b'),
    ('the fork waker status', r'waker->status|\bZEND_ASYNC_WAKER_(?:NO_STATUS|WAITING|QUEUED|IGNORED|RESULT)\b'),
    ('the fork yield flag', r'\bF_YIELD\b|\bIS_YIELD\b'),
    ('a fork wait structure', r'\b(?:triggered_events|inline_triggers|current_iterator|resume_when|resumed_coroutines)\b'),
    ('a fiber switch ban or its check (P1.4: the scheduler-context flag)', r'\bzend_fiber_switch_(?:un)?block(?:ed|ing)?\b'),
]

# (file, flag it must pass to the compiler), looked for outside comments.
REQUIRED = [
    ('config.m4', '-Werror=incompatible-pointer-types'),
    ('config.m4', '-Werror=implicit-function-declaration'),
    ('config.w32', '/we4133'),
]
COMMENT = re.compile(r'^\s*(?:dnl\b|#|//)')


def sources():
    yield ROOT / 'php_true_async.h'
    yield from sorted((ROOT / 'src').rglob('*.[ch]'))


def main():
    errors = []

    for path in sources():
        for number, line in enumerate(path.read_text().splitlines(), 1):
            where = f'{path.relative_to(ROOT)}:{number}'

            for what, pattern in FORBIDDEN:
                if re.search(pattern, line):
                    errors.append(f'{where}: {what}: {line.strip()}')

            for name in re.findall(r'\bzend_async_\w*_t\b', line):
                if name not in CORE_TYPES:
                    errors.append(f'{where}: a fork type name, renamed async_* by D12: {name}')

    for name, flag in REQUIRED:
        code = [line for line in (ROOT / name).read_text().splitlines() if not COMMENT.match(line)]

        if not any(flag in line for line in code):
            errors.append(f'{name}: the build flag {flag} is missing')

    for message in errors:
        print(message)

    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
