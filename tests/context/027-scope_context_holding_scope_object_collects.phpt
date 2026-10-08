--TEST--
Context: a scope with no coroutine left whose context holds the scope's object is collected by gc_collect_cycles()
--FILE--
<?php

use function Async\await;
use function Async\current_context;

class Marker
{
    public function __destruct()
    {
        echo "marker freed\n";
    }
}

$scope = Async\Scope::inherit();
$context = await($scope->spawn(fn() => current_context()));
$context->set('scope', $scope);
$context->set('marker', new Marker());
$weak_scope = WeakReference::create($scope);
unset($scope, $context);

$collected = gc_collect_cycles();
echo "collected: ", var_export($collected > 0, true), "\n";
var_dump($weak_scope->get());

?>
--EXPECT--
marker freed
collected: true
NULL
