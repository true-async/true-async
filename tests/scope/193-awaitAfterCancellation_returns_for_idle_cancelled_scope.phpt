--TEST--
Scope: awaitAfterCancellation() returns at once for a scope a cancel closed while idle
--FILE--
<?php
use Async\Scope;
use function Async\spawn;
use function Async\await;

$empty = Scope::inherit();
$empty->spawn(function () {});
Async\suspend();
$empty->cancel();
await(spawn(function () use ($empty) {
    $empty->awaitAfterCancellation();
    echo "empty: returned\n";
}));
?>
--EXPECT--
empty: returned
