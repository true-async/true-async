--TEST--
Scope: the Coroutine::finally() handler of a member of a child scope, cancelled by the parent scope's disposeAfterTimeout() fire, is not called
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
        echo "member cancelled\n";
        throw $cancellation;
    }
});

while (!$started) {
    suspend();
}

$parent->disposeAfterTimeout(10);
delay(60);
echo "end\n";
?>
--EXPECT--
member cancelled
end
