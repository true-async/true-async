--TEST--
Scope: the GC leaves a scope object in a cycle with its handler while a member runs, and the handler gets the object itself
--FILE--
<?php

use Async\Scope;
use function Async\delay;
use function Async\suspend;

function start(): Async\Coroutine
{
    $scope = Scope::inherit();
    $scope->setExceptionHandler(function (Scope $s, Async\Coroutine $c, Throwable $e) use ($scope) {
        echo "handler: ", $e->getMessage(), ", stand-in: ", var_export($s !== $scope, true), "\n";
    });

    $member = $scope->spawn(function () {
        delay(20);
        throw new RuntimeException("after the GC");
    });
    while (!$member->isStarted()) {
        suspend();
    }

    return $member;
}

$member = start();
// The member reaches the handler through the error route, so the object is live: no destructor, no
// zombie, no stand-in.
gc_collect_cycles();
gc_collect_cycles();
echo "gc ran\n";

while (!$member->isCompleted()) {
    delay(10);
}
echo "end\n";

?>
--EXPECT--
gc ran
handler: after the GC, stand-in: false
end
