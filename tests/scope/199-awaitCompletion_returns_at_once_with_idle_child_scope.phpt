--TEST--
Scope: awaitCompletion() returns at once on a scope whose child scopes have no coroutine
--FILE--
<?php

use Async\Scope;

// A coroutine that would run first if the wait suspended.
Async\spawn(fn() => print("other coroutine\n"));
$idle = new Scope();
$idle_child = Scope::inherit($idle);
$idle->awaitCompletion(Async\timeout(1000));
echo "idle child scope: returned\n";

?>
--EXPECT--
idle child scope: returned
other coroutine
