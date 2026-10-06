--TEST--
With nothing runnable the scheduler parks in the reactor's queue, and each Timer op wakes its waiter by its deadline
--FILE--
<?php
use function Async\spawn;
use TrueAsync\Test;

function state(): string
{
    $state = Test\reactor_state();

    return "queue " . var_export($state['queue'], true) . ", waits {$state['waits']}";
}

echo "before: ", state(), "\n";

$started = hrtime(true);
$late = spawn(function () { Test\reactor_wait(60); echo "60 ms\n"; });
$early = spawn(function () { Test\reactor_wait(20); echo "20 ms\n"; });

Test\reactor_wait(100);
echo "100 ms, slept enough: ", var_export(hrtime(true) - $started >= 100 * 1000000, true), "\n";
echo "after: ", state(), "\n";
?>
--EXPECT--
before: queue false, waits 0
20 ms
60 ms
100 ms, slept enough: true
after: queue true, waits 0
