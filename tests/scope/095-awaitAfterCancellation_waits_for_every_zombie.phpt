--TEST--
Scope: awaitAfterCancellation() returns once every zombie has finished; TrueAsync returns at the first one (probe s9.5/d3)
--FILE--
<?php
use Async\Scope;
use function Async\spawn;
use function Async\await;
use function Async\delay;

$scope = Scope::inherit();
$child = Scope::inherit($scope);
$scope->spawn(function () { delay(10); echo "zombie 1 done\n"; });
$child->spawn(function () { delay(60); echo "zombie 2 done\n"; });
Async\suspend();
$scope->cancel();

await(spawn(function () use ($scope) {
    $scope->awaitAfterCancellation();
    echo "awaitAfterCancellation returned\n";
}));
var_dump($scope->isClosed());
?>
--EXPECT--
zombie 1 done
zombie 2 done
awaitAfterCancellation returned
bool(true)
