--TEST--
Scope: the disposeAfterTimeout() fire of a scope whose members all returned calls the scope's own Scope::finally() handler
--FILE--
<?php

use function Async\delay;

$scope = new Async\Scope();
$scope->finally(function () {
    echo "scope finally runs\n";
});
$scope->spawn(function () {
    delay(5);
});
$scope->disposeAfterTimeout(50);
delay(100);
echo "end\n";
?>
--EXPECT--
scope finally runs
end
