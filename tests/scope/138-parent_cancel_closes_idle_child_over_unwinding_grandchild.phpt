--TEST--
Scope: the cancel of a completed parent closes an idle child scope whose cancelled grandchild still unwinds, and leaves the grandchild open
--FILE--
<?php

use Async\Scope;
use function Async\await;
use function Async\delay;

$parent = new Scope();
$child = Scope::inherit($parent);
$grandchild = Scope::inherit($child);
$started = false;
$coroutine = $grandchild->spawn(function () use (&$started) {
    $started = true;

    try {
        delay(1000);
    } catch (Async\AsyncCancellation) {
    }

    delay(10);
});

while (!$started) {
    Async\suspend();
}

$grandchild->cancel();
$parent->cancel();
echo "child closed: ", var_export($child->isClosed(), true), "\n";
echo "grandchild closed: ", var_export($grandchild->isClosed(), true), "\n";
await($coroutine);
?>
--EXPECT--
child closed: true
grandchild closed: false
