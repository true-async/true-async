<?php
// B5: many waiters on one target (dev/plans/S3.md, section 12).
// Usage: b5.php <waiters> <rounds>. One operation is one waiter: spawn, await, wake, finish.
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$count = (int) ($argv[1] ?? 1000);
$rounds = (int) ($argv[2] ?? 100);
$waiter = static function ($target) {
    return await($target);
};

for ($round = 0; $round < $rounds; $round++) {
    $target = spawn(static function () {
        suspend();
        return 1;
    });
    $waiters = [];

    for ($i = 0; $i < $count; $i++) {
        $waiters[] = spawn($waiter, $target);
    }

    foreach ($waiters as $coroutine) {
        await($coroutine);
    }
}
