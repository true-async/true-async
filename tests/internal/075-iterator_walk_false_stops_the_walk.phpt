--TEST--
The iterator core stops the walk of an array when the callback returns false
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\Scope;
use function TrueAsync\Test\iterate;

function run(string $name, Closure $walk): void
{
    echo "--- $name\n";
    $scope = new Scope();
    $scope->spawn($walk);
    $scope->awaitCompletion(new Future(new FutureState()));
}

run('false stops the walk', function () {
    iterate([1, 2, 3], function ($value) {
        echo "value $value\n";
        return $value < 2;
    });
});

?>
--EXPECT--
--- false stops the walk
value 1
value 2
