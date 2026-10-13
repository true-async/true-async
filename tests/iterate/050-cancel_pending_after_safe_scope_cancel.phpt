--TEST--
iterate(): cancelPending cancels a child that a safe cancel of the caller's scope left running
--FILE--
<?php

use Async\Scope;
use function Async\await;
use function Async\iterate;
use function Async\spawn;
use function Async\suspend;

$scope = (new Scope())->allowZombies();
$started = false;
$cancelled = false;

$caller = $scope->spawn(function () {
    iterate([1], function () {
        spawn(function () {
            global $started;
            $started = true;
            try {
                for ($i = 0; $i < 50; $i++) {
                    suspend();
                }
                echo "child ended its loop\n";
            } finally {
                echo "child finally\n";
            }
        });
        while (!$GLOBALS['cancelled']) {
            suspend();
        }
        echo "callback finished\n";
    }, cancelPending: true);
    echo "iterate returned\n";
});

while (!$started) {
    suspend();
}

$scope->cancel();
$cancelled = true;
await($caller);
echo "caller cancelled: ", var_export($caller->isCancelled(), true), "\n";
?>
--EXPECT--
callback finished
child finally
iterate returned
caller cancelled: true
