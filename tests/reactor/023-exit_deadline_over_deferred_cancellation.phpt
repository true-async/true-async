--TEST--
D16: a cancellation deferred by protect() does not replace the deadline's graceful exit when it unwinds protect(), so no catch around protect() sees it
--FILE--
<?php
use function Async\{spawn, delay, protect};
use TrueAsync\Test;

Test\set_exit_deadline(200);

$exited = 0;

$worker = spawn(function () use (&$exited) {
    try {
        protect(function () use (&$exited) {
            try {
                delay(60000);
            } catch (Async\AsyncCancellation $e) {
                echo "caught in protect(): ", $e->getMessage(), "\n";
            }

            try {
                delay(3000);
            } finally {
                $elapsed = (hrtime(true) - $exited) / 1e6;
                echo "inner finally: ", $elapsed < 2000 ? "unwound at the deadline" : "ran out ($elapsed ms)", "\n";
            }
        });
    } catch (Async\AsyncCancellation $e) {
        echo "caught after protect(): ", $e->getMessage(), "\n";
    } finally {
        echo "outer finally\n";
    }

    echo "not reached\n";
});

Async\suspend();
$worker->cancel(new Async\AsyncCancellation("deferred"));
$exited = hrtime(true);
exit(0);
?>
--EXPECT--
caught in protect(): Graceful shutdown
inner finally: unwound at the deadline
outer finally
