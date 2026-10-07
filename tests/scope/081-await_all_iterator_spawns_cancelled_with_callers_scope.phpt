--TEST--
Scope: what a Traversable spawns while await_all() walks it belongs to a child scope of the caller's, which the caller's cancel() reaches
--FILE--
<?php

use Async\Scope;
use function Async\spawn;
use function Async\delay;
use function Async\await_all;

function items(): Generator
{
    yield spawn(function () {
        try {
            delay(1000);
            echo "item woke\n";
        } catch (Throwable $e) {
            echo "item: ", $e->getMessage(), "\n";
        }
    });
}

$scope = Scope::inherit()->asNotSafely();
$caller = $scope->spawn(function () {
    try {
        await_all(items());
    } catch (Throwable $e) {
        echo "caller: ", get_class($e), ": ", $e->getMessage(), "\n";
    }
});

delay(20);
$scope->cancel();
delay(20);
echo "end\n";

?>
--EXPECT--
item: Scope was cancelled
caller: Async\AsyncCancellation: Scope was cancelled
end
