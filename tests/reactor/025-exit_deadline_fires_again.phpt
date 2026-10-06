--TEST--
D16: after the deadline, a finally that waits and a coroutine spawned in it are unwound by the next fire; once the drain ends, a shutdown function's wait is not cut
--FILE--
<?php
use function Async\{spawn, delay};
use TrueAsync\Test;

Test\set_exit_deadline(100);

$exited = 0;

spawn(function () use (&$exited) {
    try {
        delay(60000);
    } catch (Async\AsyncCancellation $e) {
        try {
            delay(60000);
        } finally {
            echo "finally runs\n";
            spawn(function () use (&$exited) {
                try {
                    delay(3000);
                } finally {
                    $elapsed = (hrtime(true) - $exited) / 1e6;
                    echo "spawned in finally: ", $elapsed < 2000 ? "unwound" : "ran out ($elapsed ms)", "\n";
                }
            });

            try {
                delay(3000);
            } finally {
                $elapsed = (hrtime(true) - $exited) / 1e6;
                echo "wait in finally: ", $elapsed < 2000 ? "unwound" : "ran out ($elapsed ms)", "\n";
            }
        }
    }
});

Async\suspend();
$exited = hrtime(true);
register_shutdown_function(function () use ($exited) {
    delay(300);
    echo "shutdown function's delay ended\n";
    $elapsed = (hrtime(true) - $exited) / 1e6;
    echo $elapsed < 4000 ? "ended within the bound" : "ended after $elapsed ms", "\n";
});
exit(0);
?>
--EXPECT--
finally runs
wait in finally: unwound
spawned in finally: unwound
shutdown function's delay ended
ended within the bound
