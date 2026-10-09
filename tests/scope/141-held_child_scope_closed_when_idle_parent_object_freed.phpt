--TEST--
Scope: the free of an idle parent scope's object closes a child scope the script still holds
--FILE--
<?php

use Async\Scope;

function child_of_freed_parent(): Scope
{
    $parent = new Scope();

    return Scope::inherit($parent);
}

$child = child_of_freed_parent();

try {
    $child->spawn(fn() => null);
} catch (Async\AsyncException $exception) {
    echo $exception->getMessage(), "\n";
}
?>
--EXPECT--
Cannot spawn a coroutine in a closed scope
