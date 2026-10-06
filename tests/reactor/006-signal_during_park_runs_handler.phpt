--TEST--
A signal that interrupts the idle wait runs its pcntl handler at once, and the wait goes on until its own deadline
--EXTENSIONS--
pcntl
--FILE--
<?php
use TrueAsync\Test;

pcntl_async_signals(true);
$started = hrtime(true);
pcntl_signal(SIGUSR1, function () use ($started) {
    echo "handler before the deadline: ", var_export(hrtime(true) - $started < 1000 * 1000000, true), "\n";
});

$pid = getmypid();
exec("(sleep 0.1; kill -USR1 $pid) > /dev/null 2>&1 &");
Test\reactor_wait(1000);
echo "woken after the deadline: ", var_export(hrtime(true) - $started >= 1000 * 1000000, true), "\n";
?>
--EXPECT--
handler before the deadline: true
woken after the deadline: true
