--TEST--
Scope: the error of a waiting destructor of what a Coroutine::finally() handler holds goes to the scope's exception handler
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\delay;

final class Connection
{
    public function __destruct()
    {
        delay(1);
        throw new RuntimeException("close failed");
    }
}

$handled = new FutureState();
$scope = new Async\Scope();
$scope->setExceptionHandler(function ($scope, $coroutine, $exception) use ($handled) {
    echo get_class($exception), ": ", $exception->getMessage(), "\n";
    $handled->complete(null);
});
$connection = new Connection();
$scope->spawn(function () {
})->finally(function () use ($connection) {
    echo "finally runs\n";
});
unset($connection);
await(new Future($handled));
echo "end\n";
?>
--EXPECT--
finally runs
RuntimeException: close failed
end
