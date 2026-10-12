--TEST--
D16: after the deadline, a finally that waits and a coroutine spawned in it are unwound by the next fire; once the drain ends, a shutdown function's wait is not cut
--FILE--
<?php
use function Async\{spawn, delay};
use TrueAsync\Test;

Test\set_exit_deadline(100);

spawn(function () {
    try {
        delay(60000);
    } catch (Async\AsyncCancellation $e) {
        try {
            delay(60000);
        } finally {
            echo "finally runs\n";
            spawn(function () {
                try {
                    delay(3000);
                    echo "spawned in finally: the wait ran out\n";
                } finally {
                    echo "spawned in finally: unwound\n";
                }
            });

            try {
                delay(3000);
                echo "wait in finally: the wait ran out\n";
            } finally {
                echo "wait in finally: unwound\n";
            }
        }
    }
});

Async\suspend();
register_shutdown_function(function () {
    delay(300);
    echo "shutdown function's delay ended\n";
});
exit(0);
?>
--EXPECT--
finally runs
wait in finally: unwound
spawned in finally: unwound
shutdown function's delay ended
