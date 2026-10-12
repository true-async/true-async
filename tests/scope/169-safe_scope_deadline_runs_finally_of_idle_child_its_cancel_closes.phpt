--TEST--
Scope: the disposeAfterTimeout() fire of a safe scope that closes an idle child scope runs the child's Scope::finally() handler
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$parent = Async\Scope::inherit();
$child = Async\Scope::inherit($parent);
$child->finally(function () {
    echo "child finally ends\n";
    $GLOBALS['ended'] = true;
});
$started = false;
$ended = false;
$parent->spawn(function () use (&$started) {
    $started = true;

    // Keeps the parent running until the fire; a fixed delay could end before it under load
    while (!$GLOBALS['ended']) {
        delay(1);
    }
});

while (!$started) {
    suspend();
}

$parent->disposeAfterTimeout(10);

// A longer delay could end first: a timer due when main parks wakes it in place, ahead of the queue
while (!$ended) {
    delay(1);
}

echo "end\n";
?>
--EXPECT--
child finally ends
end
