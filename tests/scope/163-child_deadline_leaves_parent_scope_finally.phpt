--TEST--
Scope: a child scope's disposeAfterTimeout() fire leaves its parent's Scope::finally() handler to run
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$parent = new Async\Scope();
$parent->finally(function () {
    echo "parent finally runs\n";
});
$child = Async\Scope::inherit($parent);
$started = false;
$child->spawn(function () use (&$started) {
    $started = true;
    delay(100000);
});

while (!$started) {
    suspend();
}

$child->disposeAfterTimeout(10);
delay(60);
$parent->dispose();
delay(10);
echo "end\n";
?>
--EXPECT--
parent finally runs
end
