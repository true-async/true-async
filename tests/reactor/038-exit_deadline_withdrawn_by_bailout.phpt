--TEST--
D16: a fatal error that ends the drain withdraws the deadline, so a shutdown function's wait after it is not cut
--FILE--
<?php
use function Async\{spawn, delay};
use TrueAsync\Test;

Test\set_exit_deadline(200);

spawn(function () {
    try {
        delay(60000);
    } finally {
        eval('function twice() {} function twice() {}');
    }
});

Async\suspend();
register_shutdown_function(function () {
    $started = hrtime(true);
    delay(400);
    $elapsed = (hrtime(true) - $started) / 1e6;
    echo "shutdown function's delay ", $elapsed >= 390 ? "ran out" : "was cut after $elapsed ms", "\n";
});
exit(0);
?>
--EXPECTF--
Fatal error: Cannot redeclare function twice() %A
shutdown function's delay ran out
