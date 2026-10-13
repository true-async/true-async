--TEST--
With nothing runnable the scheduler parks in the reactor's queue, and each Timer op wakes its waiter by its deadline
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use TrueAsync\Test;

function state(): string
{
    $state = Test\reactor_state();

    return "queue " . var_export($state['queue'], true) . ", waits {$state['waits']}";
}

function sleeper(int $ms): bool
{
    $started = hrtime(true);
    Test\reactor_wait($ms);

    return hrtime(true) - $started >= $ms * 1000000;
}

echo "before: ", state(), "\n";

// The other waiter's timer never fires: an idle wait that went by the farthest deadline would never end.
$never = spawn(fn() => Test\reactor_wait(-1));
$early = spawn(fn() => sleeper(20));

echo "20 ms, slept enough: ", var_export(await($early), true), "\n";
$never->cancel();

try {
    await($never);
} catch (Async\AsyncCancellation) {
}

echo "after: ", state(), "\n";
?>
--EXPECT--
before: queue false, waits 0
20 ms, slept enough: true
after: queue true, waits 0
