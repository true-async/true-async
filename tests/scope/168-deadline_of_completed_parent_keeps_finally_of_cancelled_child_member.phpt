--TEST--
Scope: a member of a cancelled child scope that goes on after catching the cancellation starts its Coroutine::finally() handler when it finishes after the disposeAfterTimeout() fire of the child scope's completed parent, which does not reach it
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$parent = new Async\Scope();
$child = Async\Scope::inherit($parent);
$started = false;
$child->spawn(function () use (&$started) {
    Async\current_coroutine()->finally(function () {
        echo "finally runs\n";
    });
    $started = true;

    try {
        delay(100000);
    } catch (Async\AsyncCancellation $cancellation) {
        delay(50);
        echo "member done\n";
    }
});

while (!$started) {
    suspend();
}

$child->cancel();
$parent->disposeAfterTimeout(10);
delay(100);
echo "end\n";
?>
--EXPECT--
member done
finally runs
end
