--TEST--
Scope: a held child scope refuses spawn() after the cancel of its completed parent
--FILE--
<?php

use Async\Scope;
use function Async\await;

$parent = new Scope();
$child = Scope::inherit($parent);
await($child->spawn(fn() => null));
$parent->cancel();

try {
    $child->spawn(fn() => null);
} catch (Async\AsyncException $exception) {
    echo $exception->getMessage(), "\n";
}
?>
--EXPECT--
Cannot spawn a coroutine in a closed scope
