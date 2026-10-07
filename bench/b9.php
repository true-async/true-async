<?php
// B9: await_all() over N pending Futures, completed after the waiter parks (dev/plans/S5.md,
// "Measurements"; S3.md section 12, the await_* row). Usage: b9.php <n> <rounds>. One operation is
// one await_all(): its links, the wake, the results.
use Async\Future;
use Async\FutureState;
use function Async\await_all;
use function Async\spawn;

$count = (int) ($argv[1] ?? 8);
$rounds = (int) ($argv[2] ?? 1000);
$complete = static function (array $states) {
    foreach ($states as $i => $state) {
        $state->complete($i);
    }
};

for ($round = 0; $round < $rounds; $round++) {
    $states = [];
    $futures = [];

    for ($i = 0; $i < $count; $i++) {
        $states[] = $state = new FutureState();
        $futures[] = new Future($state);
    }

    spawn($complete, $states);
    await_all($futures);
}
