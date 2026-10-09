--TEST--
Scope: the disposeAfterTimeout() fire of a scope whose only running coroutine is a finally handler's worker leaves the scope's own Scope::finally() handler to be called
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$scope = new Async\Scope();
$scope->finally(function () {
    echo "scope finally runs\n";
});
$in_finally = false;
$scope->spawn(function () use (&$in_finally) {
    Async\current_coroutine()->finally(function () use (&$in_finally) {
        $in_finally = true;

        delay(100000);
    });
});

while (!$in_finally) {
    suspend();
}

$scope->disposeAfterTimeout(10);
delay(60);
echo "end\n";
?>
--EXPECT--
scope finally runs
end
