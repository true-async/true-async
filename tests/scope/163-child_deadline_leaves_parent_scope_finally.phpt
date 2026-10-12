--TEST--
Scope: a child scope's disposeAfterTimeout() fire leaves its parent's Scope::finally() handler to run
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$ended = false;
$parent = new Async\Scope();
$parent->finally(function () {
    echo "parent finally runs\n";
    $GLOBALS['ended'] = true;
});
$child = Async\Scope::inherit($parent);
$started = false;
$member = $child->spawn(function () use (&$started) {
    $started = true;
    delay(100000);
});

while (!$started) {
    suspend();
}

$child->disposeAfterTimeout(10);

// A longer delay could end first: a timer due when main parks wakes it in place, ahead of the queue
while (!$member->isCompleted()) {
    delay(1);
}

$parent->dispose();

while (!$ended) {
    suspend();
}

echo "end\n";
?>
--EXPECT--
parent finally runs
end
