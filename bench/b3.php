<?php
// B3: one coroutine suspending alone, the tick without a switch (dev/plans/S3.md, section 12).
// Usage: b3.php <suspends>. One operation is one suspend().
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$count = (int) ($argv[1] ?? 2000000);
await(spawn(static function () use ($count) {
    for ($i = 0; $i < $count; $i++) {
        suspend();
    }
}));
