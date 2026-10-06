--TEST--
A bailout unwinds the coroutines parked on Timer ops: their records are unlinked and their ops withdrawn from the queue (the Poll queue) before the shutdown functions run
--FILE--
<?php
use function Async\spawn;
use TrueAsync\Test;

Test\reactor_use_poll_queue();

register_shutdown_function(function () {
    $state = Test\reactor_state();
    echo "shutdown, waits: {$state['waits']}, pending: {$state['pending']}\n";
});

spawn(fn() => Test\reactor_wait(60000));
spawn(fn() => Test\reactor_wait(60000));
Async\suspend();
echo "waits before the bailout: ", Test\reactor_state()['waits'], "\n";
eval('function twice() {} function twice() {}');
?>
--EXPECTF--
waits before the bailout: 2

Fatal error: Cannot redeclare function twice() %s
shutdown, waits: 0, pending: 0
