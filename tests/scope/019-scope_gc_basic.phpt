--TEST--
Scope: GC handler basic functionality
--FILE--
<?php

use Async\Scope;
use function Async\spawn;
use function Async\suspend;

// Test that GC handler is registered and functioning for scope
$scope = new Scope();
$ref = WeakReference::create($scope);

// The handler holds the scope, a cycle only the GC handler can walk
$scope->setExceptionHandler(function() use ($scope) {});

$coroutine = $scope->spawn(function() {
    return "scope_test_value";
});

// Force garbage collection to ensure our GC handler is called
gc_collect_cycles();

suspend(); // Suspend to simulate coroutine lifecycle

// Check that coroutine completed successfully
$result = $coroutine->getResult();
var_dump($result);

// The cycle goes once nothing else holds the scope
unset($scope, $coroutine);
gc_collect_cycles();
var_dump($ref->get() === null);

?>
--EXPECT--
string(16) "scope_test_value"
bool(true)