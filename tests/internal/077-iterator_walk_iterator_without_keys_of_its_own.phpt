--TEST--
The iterator core walks an iterator without keys of its own
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

run('an iterator without keys of its own', function () {
    iterate(new ArrayIterator([5, 6]), function ($value, $key) {
        echo "$key=$value\n";
    });
});

?>
--EXPECT--
--- an iterator without keys of its own
0=5
1=6
