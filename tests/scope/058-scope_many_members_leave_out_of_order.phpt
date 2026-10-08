--TEST--
Scope: 100 000 members leave in another order than they joined; a cancel then reaches each one left
--CONFLICTS--
fiber_stacks
--INI--
zend.enable_gc=0
--FILE--
<?php

use Async\Scope;
use function Async\suspend;
use function TrueAsync\Test\coroutine_count;

// GC is off so all 100 000 join before any runs: with GC on, main resumes after each GC run of the
// spawn loop at the tail of the queue, behind the coroutines spawned so far.
$scope = new Scope();
$count = 100000;
$survivors = [];

// Every tenth coroutine runs until it is cancelled, one in a hundred yields once, the rest finish on
// their first run. Windows commits each suspended coroutine's 2 MB stack in full, and 20 000 of them
// pass the CI runner's commit limit.
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
int(10001)
int(10000)
bool(true)
array(0) {
}
