<?php
// B23: iterate() over an array whose callbacks suspend once (dev/plans/S9-iterate.md, section 8).
// Usage: b23.php <elements> [concurrency] [known]. Concurrency 0 (the default) is no limit, B23-10 passes 10.
// One operation is one element with its callback's yield. "known" adds a `new stdClass` per element: the
// known-answer variant.
use function Async\iterate;

gc_disable();
$elements = (int) ($argv[1] ?? 10000);
$concurrency = (int) ($argv[2] ?? 0);
$known = ($argv[3] ?? '') === 'known';

iterate(range(1, $elements), static function () use ($known) {
    $extra = $known ? new stdClass() : null;
    Async\suspend();
}, $concurrency);
