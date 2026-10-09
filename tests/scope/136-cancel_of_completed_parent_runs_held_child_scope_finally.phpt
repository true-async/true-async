--TEST--
Scope: cancel() of a completed parent runs the finally handler of a child scope the script still holds
--FILE--
<?php

use Async\Scope;
use function Async\await;

$parent = new Scope();
$child = Scope::inherit($parent);
$finally_ran = false;
$child->finally(function () use (&$finally_ran) {
    echo "child finally\n";
    $finally_ran = true;
});
await($child->spawn(fn() => null));

$parent->cancel();

while (!$finally_ran) {
    Async\suspend();
}

echo "end\n";
?>
--EXPECT--
child finally
end
