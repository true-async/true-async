--TEST--
Scope: the disposeAfterTimeout() fire that closes an idle child scope runs the child's Scope::finally() handler
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
$parent->spawn(function () use (&$started) {
    $started = true;
    delay(100000);
});

while (!$started) {
    suspend();
}

$parent->disposeAfterTimeout(10);
delay(60);
echo "end\n";
?>
--EXPECT--
child finally ends
end
