--TEST--
Scope: 20 members leave in another order than they joined; a cancel then reaches each one left
--CONFLICTS--
fiber_stacks
--FILE--
<?php

use Async\Scope;
use function Async\suspend;
use function TrueAsync\Test\coroutine_count;

$scope = new Scope();
$count = 20;
$survivors = [];

// Every tenth coroutine runs until it is cancelled, kind 5 yields once, the rest finish on their first run.
for ($i = 0; $i < $count; $i++) {
    $coroutine = $scope->spawn(function (int $kind) {
        if ($kind % 10 === 0) {
            for (;;) {
                suspend();
            }
        }

        if ($kind === 5) {
            suspend();
        }
    }, $i % 100);

    if ($i % 10 === 0) {
        $survivors[] = $coroutine;
    }
}

for ($i = 0; $i < 4; $i++) {
    suspend();
}

var_dump(coroutine_count());

$scope->cancel();

while (coroutine_count() > 1) {
    suspend();
}

var_dump(count(array_filter($survivors, fn($coroutine) => $coroutine->isCancelled())));
var_dump($scope->isClosed(), $scope->getChildScopes());

?>
--EXPECT--
int(3)
int(2)
bool(true)
array(0) {
}
