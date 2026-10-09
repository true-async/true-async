--TEST--
Scope: a second cancel of the parent leaves a cancelled child scope open while its coroutine unwinds
--FILE--
<?php

use Async\Scope;
use function Async\await;
use function Async\delay;

$parent = new Scope();
$child = Scope::inherit($parent);
$finally_ran = false;
$child->finally(function () use (&$finally_ran) {
    echo "child finally\n";
    $finally_ran = true;
});
$started = false;
$coroutine = $child->spawn(function () use (&$started) {
    $started = true;

    try {
        delay(1000);
    } catch (Async\AsyncCancellation) {
    }

    delay(10);
    echo "child coroutine done\n";
});

while (!$started) {
    Async\suspend();
}

$parent->cancel();
$parent->cancel();
echo "child closed: ", var_export($child->isClosed(), true), "\n";
await($coroutine);

while (!$finally_ran) {
    Async\suspend();
}
?>
--EXPECT--
child closed: false
child coroutine done
child finally
