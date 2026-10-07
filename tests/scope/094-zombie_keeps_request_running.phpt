--TEST--
Scope: a zombie left by disposeSafely() keeps the request running after main ends, as TrueAsync's code (S9-scope.md 5, probe p3)
--FILE--
<?php
use Async\Scope;
use function Async\delay;

$scope = (new Scope())->allowZombies();
$scope->spawn(function () {
    delay(50);
    echo "zombie finished\n";
});
Async\suspend();
$scope->disposeSafely();
echo "main end\n";
?>
--EXPECT--
main end
zombie finished
