--TEST--
Context: the object of a scope whose zombie still runs is not collected while the scope's context holds it, and the zombie reads it
--FILE--
<?php

use function Async\current_context;
use function Async\delay;
use function Async\suspend;

$scope = Async\Scope::inherit();
$scope->spawn(function () {
    delay(20);
    echo "zombie reads: ", get_debug_type(current_context()->find('scope')), "\n";
});
suspend();
$scope->spawn(fn() => current_context()->set('scope', $scope))->getResult();
suspend();
suspend();
$scope->disposeSafely();
unset($scope);

echo "collected: ", gc_collect_cycles(), "\n";
delay(50);
echo "end\n";

?>
--EXPECT--
collected: 0
zombie reads: Async\Scope
end
