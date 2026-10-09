--TEST--
Scope: a member that disposeSafely() left running as a zombie starts its Coroutine::finally() handler when it finishes after a later disposeAfterTimeout() fire, which does not reach it
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$scope = new Async\Scope();
$started = false;
$scope->spawn(function () use (&$started) {
    Async\current_coroutine()->finally(function () {
        echo "finally runs\n";
    });
    $started = true;
    delay(50);
    echo "member done\n";
});

while (!$started) {
    suspend();
}

$scope->disposeSafely();
$scope->disposeAfterTimeout(10);
delay(100);
echo "end\n";
?>
--EXPECT--
member done
finally runs
end
