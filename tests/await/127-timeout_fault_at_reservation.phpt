--TEST--
A fatal error at the reservation of a wait with a Timeout leaves no armed timer behind: a shutdown function's wait is reported as a deadlock at once
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use function Async\await;
use function Async\spawn;
use TrueAsync\Test;

$target = spawn(function () {
    Async\suspend();
    echo "not reached: target\n";
});

$waiter = spawn(function () use ($target) {
    Test\fail_at('reserve');
    await($target, Async\timeout(60000));
});

register_shutdown_function(function () {
    echo "shutdown: waits ", Test\reactor_state()['waits'], "\n";
});

echo "main end\n";
?>
--EXPECTF--
main end

Fatal error: Fault injected at reserve in %s on line %d
shutdown: waits 0
