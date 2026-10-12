--TEST--
Context: the object of a scope whose zombie still runs is not collected while the scope's context holds it, and the zombie reads it
--FILE--
<?php

use function Async\current_context;
use function Async\delay;
use function Async\suspend;

$started = false;
$collected = false;
$read = false;
$scope = Async\Scope::inherit();
$scope->spawn(function () {
    $GLOBALS['started'] = true;

    // Parked on a timer until main has collected: one delay could end before that
    while (!$GLOBALS['collected']) {
        delay(1);
    }

    echo "zombie reads: ", get_debug_type(current_context()->find('scope')), "\n";
    $GLOBALS['read'] = true;
});

// Under a seed one suspend() need not run it, and a zombie that never started would never read
while (!$started) {
    suspend();
}

$scope->spawn(fn() => current_context()->set('scope', $scope))->getResult();
suspend();
suspend();
$scope->disposeSafely();
unset($scope);

echo "collected: ", gc_collect_cycles(), "\n";
$collected = true;

while (!$read) {
    delay(1);
}

echo "end\n";

?>
--EXPECT--
collected: 0
zombie reads: Async\Scope
end
