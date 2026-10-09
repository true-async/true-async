--TEST--
Scope: the disposeAfterTimeout() fire of a scope whose members all returned calls the scope's own Scope::finally() handler
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use function Async\await;

$scope = new Async\Scope();
$state = new FutureState();
$scope->finally(function () use ($state) {
    echo "scope finally runs\n";
    $state->complete(null);
});
await($scope->spawn(function () {
}));
// An idle child scope keeps the timer from being refused on an empty scope.
$child = Async\Scope::inherit($scope);
$scope->disposeAfterTimeout(10);
await(new Future($state));
echo "end\n";
?>
--EXPECT--
scope finally runs
end
