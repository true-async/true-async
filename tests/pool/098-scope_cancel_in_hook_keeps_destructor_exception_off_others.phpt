--TEST--
Pool: a scope cancel inside beforeAcquire whose destructor throws: no coroutine's cancellation takes that exception, which is reported as uncaught
--FILE--
<?php

$scope = new Async\Scope();
$parked = 0;

$pool = new Async\Pool(
    factory: fn() => 'r',
    destructor: function ($resource) {
        throw new RuntimeException("destructor failed");
    },
    beforeAcquire: function ($resource) use (&$parked) {
        $parked++;
        Async\delay(10000);

        return true;
    },
    min: 1,
);

$in_hook = $scope->spawn(function () use ($pool) {
    try {
        $pool->acquire();
    } catch (Async\AsyncCancellation $e) {
        echo "in hook: previous ", $e->getPrevious() === null ? "none" : get_class($e->getPrevious()), "\n";
    }
});

$other = $scope->spawn(function () use (&$parked) {
    $parked++;

    try {
        Async\delay(10000);
    } catch (Async\AsyncCancellation $e) {
        echo "other: previous ", $e->getPrevious() === null ? "none" : get_class($e->getPrevious()), "\n";
    }
});

while ($parked < 2) {
    Async\suspend();
}

$scope->cancel();
Async\await($in_hook);
Async\await($other);
echo "pool count: ", $pool->count(), "\n";
?>
--EXPECTF--
in hook: previous none
other: previous none
pool count: 0

Fatal error: Uncaught RuntimeException: destructor failed in %s:%d
Stack trace:
%A