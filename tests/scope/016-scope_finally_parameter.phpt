--TEST--
Scope: finally() - finally handler receives scope parameter
--XFAIL--
Not implemented yet: S9.6 of dev/PLAN.md
--FILE--
<?php

use Async\Scope;
use function Async\await;

$scope = new Scope();
$coroutine = $scope->spawn(function() { 
    return "test"; 
});

$scope->finally(function($receivedScope) use ($scope) {
    echo "Finally handler received scope: " . 
         ($receivedScope === $scope ? "correct" : "incorrect") . "\n";
});

await($coroutine);
$scope->dispose();

?>
--EXPECT--
Finally handler received scope: correct