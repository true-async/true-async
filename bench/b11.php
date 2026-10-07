<?php
// B11: N waiters on one cancellation token, each woken by its own Future (dev/plans/S3.md, section
// 12, linear unlink under fan-in). Usage: b11.php <n> <rounds>. One operation is one waiter: spawn,
// park on its Future and the shared token, wake, unlink from the token's callbacks, finish.
use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\spawn;
use function Async\suspend;

$count = (int) ($argv[1] ?? 1000);
$rounds = (int) ($argv[2] ?? 10);
$waiter = static function (FutureState $state, Future $token) {
    return await(new Future($state), $token);
};
$token_state = new FutureState();
$token = new Future($token_state);

for ($round = 0; $round < $rounds; $round++) {
    $states = [];
    $waiters = [];

    for ($i = 0; $i < $count; $i++) {
        $states[] = $state = new FutureState();
        $waiters[] = spawn($waiter, $state, $token);
    }

    suspend();

    foreach ($states as $i => $state) {
        $state->complete($i);
    }

    foreach ($waiters as $coroutine) {
        await($coroutine);
    }
}

$token_state->complete(null);
