--TEST--
D16: after exit(), a coroutine that caught the graceful cancellation and waits again inside protect() is unwound at the deadline; its finally blocks run, no catch sees it
--FILE--
<?php
use function Async\{spawn, delay, protect};
use TrueAsync\Test;

Test\set_exit_deadline(200);

spawn(function () {
    try {
        delay(60000);
    } catch (Async\AsyncCancellation $e) {
        echo "caught: ", $e->getMessage(), "\n";
        echo "armed: ", Test\reactor_state()['own'], "\n";

        try {
            protect(function () {
                try {
                    delay(3000);
                } finally {
                    echo "inner finally\n";
                }
            });
        } catch (Throwable $e) {
            echo "caught again: ", get_class($e), "\n";
        } finally {
            echo "outer finally\n";
        }

        echo "not reached\n";
    }
});

register_shutdown_function(function () {
    $state = Test\reactor_state();
    echo "shutdown: own ", $state['own'], ", waits ", $state['waits'], "\n";
});

Async\suspend();
$exited = hrtime(true);
register_shutdown_function(function () use ($exited) {
    $elapsed = (hrtime(true) - $exited) / 1e6;
    echo $elapsed >= 200 && $elapsed < 4000 ? "unwound at the deadline" : "unwound after $elapsed ms", "\n";
});
exit(0);
?>
--EXPECT--
caught: Graceful shutdown
armed: 1
inner finally
outer finally
shutdown: own 0, waits 0
unwound at the deadline
