--TEST--
A signal that interrupts the idle wait runs its pcntl handler at once, and the wait goes on until its own deadline
--EXTENSIONS--
pcntl
--FILE--
<?php
use TrueAsync\Test;

pcntl_async_signals(true);
$started = hrtime(true);
pcntl_signal(SIGUSR1, function () {
    // The wait's Timer is still in the heap until its deadline fires.
    echo "handler before the deadline: timers armed ", Test\reactor_state()['timers'], "\n";
});

$pid = getmypid();
// The shell has 2.9 s to start and send the signal before the wait ends.
exec("(sleep 0.1; kill -USR1 $pid) > /dev/null 2>&1 &");
Test\reactor_wait(3000);
echo "woken after the deadline: ", var_export(hrtime(true) - $started >= 3000 * 1000000, true), "\n";
?>
--EXPECT--
handler before the deadline: timers armed 1
woken after the deadline: true
