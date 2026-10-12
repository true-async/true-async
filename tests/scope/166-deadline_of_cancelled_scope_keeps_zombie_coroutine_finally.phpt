--TEST--
Scope: a member that disposeSafely() left running as a zombie starts its Coroutine::finally() handler when it finishes after a later disposeAfterTimeout() fire, which does not reach it
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\delay;
use function Async\suspend;

$scope = new Async\Scope();
$release = new FutureState();
$started = false;
$member = $scope->spawn(function () use ($release, &$started) {
    Async\current_coroutine()->finally(function () {
        echo "finally runs\n";
    });
    $started = true;
    await(new Future($release));
    echo "member done\n";
});

while (!$started) {
    suspend();
}

$scope->disposeSafely();
$scope->disposeAfterTimeout(10);
// Armed after the fire's timer and longer, so it fires after it: the reactor runs due timers nearest first.
delay(20);
$release->complete(null);
await($member);
echo "end\n";
?>
--EXPECT--
member done
finally runs
end
