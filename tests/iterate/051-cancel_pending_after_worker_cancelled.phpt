--TEST--
iterate(): cancelPending cancels the spawned coroutines also when the last worker ends cancelled
--FILE--
<?php

use function Async\await;
use function Async\current_coroutine;
use function Async\iterate;
use function Async\spawn;
use function Async\suspend;

$worker = null;
$started = false;

$caller = spawn(function () {
    iterate([1], function () {
        spawn(function () {
            $GLOBALS['started'] = true;
            try {
                for ($i = 0; $i < 50; $i++) {
                    suspend();
                }
                echo "child ended its loop\n";
            } finally {
                echo "child finally\n";
            }
        });
        $GLOBALS['worker'] = current_coroutine();
        while (true) {
            suspend();
        }
    }, cancelPending: true);
    echo "iterate returned\n";
});

while (!$started) {
    suspend();
}

$worker->cancel();
await($caller);
echo "worker cancelled: ", var_export($worker->isCancelled(), true), "\n";
?>
--EXPECT--
child finally
iterate returned
worker cancelled: true
