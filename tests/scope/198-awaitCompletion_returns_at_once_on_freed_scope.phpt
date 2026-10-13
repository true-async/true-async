--TEST--
Scope: awaitCompletion() returns at once on a scope that was freed
--FILE--
<?php

use Async\Scope;

// The cancelled child scope is freed with its parent once its member ends; its object stays.
$parent = new Scope();
$freed = Scope::inherit($parent);
$member = $freed->spawn(fn() => Async\delay(10000));
unset($parent);
$freed->cancel();
while (!$member->isCompleted()) {
    Async\suspend();
}

$freed->awaitCompletion(Async\timeout(1000));
echo "freed: returned\n";

?>
--EXPECT--
freed: returned
