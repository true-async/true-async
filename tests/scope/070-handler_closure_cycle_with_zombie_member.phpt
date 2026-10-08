--TEST--
Scope: the GC collects a scope object in a cycle with its handler while a zombie member keeps the scope, and the handler still runs
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
// The GC runs the object's destructor, which makes the member a zombie and detaches the scope; the
// closure stays with the scope, which keeps the object, and the handler gets a stand-in.
gc_collect_cycles();
gc_collect_cycles();
echo "collected\n";

while (!$member->isCompleted()) {
    delay(10);
}
echo "end\n";

?>
--EXPECT--
collected
handler: after the GC, stand-in: true
end
