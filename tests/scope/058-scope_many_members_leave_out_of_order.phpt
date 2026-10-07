--TEST--
Scope: 100 000 members leave in another order than they joined; a cancel then reaches each one left
--INI--
zend.enable_gc=0
--FILE--
<?php

use Async\Scope;
use function Async\suspend;
use function TrueAsync\Test\coroutine_count;

// GC is off: the fuzz lane's random pick takes the GC run off the front of the queue, and the
// coroutines that fill the root buffer meanwhile park until tens of thousands of fibers pass
// vm.max_map_count. gc/025 checks the run at the front.
$scope = new Scope();
$count = 100000;
$survivors = [];

// Every tenth coroutine runs until it is cancelled, another tenth yields once, the rest finish on their
// first run.
for ($i = 0; $i < $count; $i++) {
    $coroutine = $scope->spawn(function (int $kind) {
        if ($kind === 0) {
            for (;;) {
                suspend();
            }
        }

        if ($kind === 5) {
            suspend();
        }
    }, $i % 10);

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
