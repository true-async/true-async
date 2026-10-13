--TEST--
Scope: a zombie left by disposeSafely() keeps the request running after main ends, as TrueAsync's code (S9-scope.md 5, probe p3)
--FILE--
<?php
use Async\Scope;
use function Async\delay;

$main_ended = false;
$scope = (new Scope())->allowZombies();
$zombie = $scope->spawn(function () {
    while (!$GLOBALS['main_ended']) {
        delay(1);
    }
    echo "zombie finished\n";
});

while (!$zombie->isStarted()) {
    Async\suspend();
}

$scope->disposeSafely();
echo "main end\n";
$main_ended = true;
?>
--EXPECT--
main end
zombie finished
