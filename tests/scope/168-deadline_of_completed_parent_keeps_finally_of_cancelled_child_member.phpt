--TEST--
Scope: a member of a cancelled child scope that goes on after catching the cancellation starts its Coroutine::finally() handler when it finishes after the disposeAfterTimeout() fire of the child scope's completed parent, which does not reach it
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\delay;
use function Async\suspend;

$parent = new Async\Scope();
$child = Async\Scope::inherit($parent);
$release = new FutureState();
$started = false;
$member = $child->spawn(function () use ($release, &$started) {
    Async\current_coroutine()->finally(function () {
        echo "finally runs\n";
    });
    $started = true;

    try {
        delay(100000);
    } catch (Async\AsyncCancellation $cancellation) {
        await(new Future($release));
        echo "member done\n";
    }
});

while (!$started) {
    suspend();
}

$child->cancel();
$parent->disposeAfterTimeout(10);
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
