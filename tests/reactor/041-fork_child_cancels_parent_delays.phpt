--TEST--
A forked child's rebuild takes every parent's delay off the timer heap: the child cancels three of them after its own first delay, the heap holds none, and the parent's still fire
--EXTENSIONS--
pcntl
--FILE--
<?php
use function Async\{spawn, await, delay};
use TrueAsync\Test;

$sleepers = [];

foreach ([300, 400, 500] as $ms) {
    $sleepers[] = spawn(function () use ($ms) {
        delay($ms);

        return $ms;
    });
}

while (Test\reactor_state()['timers'] < 3) {
    Async\suspend();
}

$pid = pcntl_fork();

if ($pid === 0) {
    delay(1);
    echo "child: timers after the rebuild: ", Test\reactor_state()['timers'], "\n";

    foreach ($sleepers as $sleeper) {
        $sleeper->cancel();

        try {
            await($sleeper);
        } catch (Async\AsyncCancellation) {
        }
    }

    echo "child: timers after the cancels: ", Test\reactor_state()['timers'], "\n";

    return;
}

pcntl_waitpid($pid, $status);
echo "parent: child exit status ", pcntl_wexitstatus($status), "\n";

foreach ($sleepers as $sleeper) {
    echo "parent: woken after ", await($sleeper), " ms\n";
}
?>
--EXPECT--
child: timers after the rebuild: 0
child: timers after the cancels: 0
parent: child exit status 0
parent: woken after 300 ms
parent: woken after 400 ms
parent: woken after 500 ms
