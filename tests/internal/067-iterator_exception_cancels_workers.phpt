--TEST--
An exception from the iterator core's handler stops the walk, cancels the other workers and ends the last one
--FILE--
<?php

use Async\Scope;
use function Async\delay;
use function TrueAsync\Test\iterate;

function run(string $name, Closure $walk): void
{
    echo "--- $name\n";
    $scope = new Scope();
    $scope->setChildScopeExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $error) {
        echo "error: ", $error->getMessage(), ", previous: ",
            $error->getPrevious() === null ? 'none' : get_class($error->getPrevious()), "\n";
    });
    $scope->spawn($walk);
    $scope->awaitCompletion(Async\timeout(2000));
}

/* The walk's cancel of the scope reaches every worker, the throwing one too: the error the last worker
 * ends with carries the cancellation it was given as its previous. */
run('three workers', function () {
    iterate([1, 2, 3], function ($value) {
        echo "start $value\n";
        if ($value === 2) {
            throw new Exception('boom');
        }
        try {
            delay(10);
            echo "end $value\n";
        } catch (Async\AsyncCancellation $e) {
            echo "cancelled $value: ", $e->getMessage(), "\n";
            throw $e;
        }
    }, 3);
});

run('one worker', function () {
    iterate([1], function () {
        throw new Exception('alone');
    });
});

?>
--EXPECT--
--- three workers
start 1
start 2
cancelled 1: Cancellation of the iterator due to an exception
error: boom, previous: Async\AsyncCancellation
--- one worker
error: alone, previous: Async\AsyncCancellation
