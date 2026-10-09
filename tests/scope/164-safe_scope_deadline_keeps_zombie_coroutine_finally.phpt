--TEST--
Scope: a member that the disposeAfterTimeout() fire of a safe scope leaves running as a zombie starts its Coroutine::finally() handler when it finishes
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$scope = Async\Scope::inherit();
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

$scope->disposeAfterTimeout(10);
delay(100);
echo "end\n";
?>
--EXPECT--
member done
finally runs
end
