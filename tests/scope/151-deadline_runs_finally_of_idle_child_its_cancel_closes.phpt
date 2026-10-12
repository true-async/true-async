--TEST--
Scope: the disposeAfterTimeout() fire of a scope with a running member does not call the Scope::finally() handler of the idle child scope it closes
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$parent = new Async\Scope();
$child = Async\Scope::inherit($parent);
$child->finally(function () {
    echo "child finally ends\n";
});
$started = false;
$member = $parent->spawn(function () use (&$started) {
    $started = true;
    delay(100000);
});

while (!$started) {
    suspend();
}

$parent->disposeAfterTimeout(10);

// A longer delay could end first: a timer due when main parks wakes it in place, ahead of the queue
while (!$member->isCompleted()) {
    delay(1);
}

echo "end\n";
?>
--EXPECT--
end
