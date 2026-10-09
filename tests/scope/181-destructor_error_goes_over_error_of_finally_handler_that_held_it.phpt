--TEST--
Scope: the error a destructor of what a Coroutine::finally() handler holds throws has the handler's error as its previous
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use function Async\await;

final class Connection
{
    public function __destruct()
    {
        throw new RuntimeException("close failed");
    }
}

$handled = new FutureState();
$scope = new Async\Scope();
$scope->setExceptionHandler(function ($scope, $coroutine, $exception) use ($handled) {
    echo get_class($exception), ": ", $exception->getMessage(), "\n";
    echo "previous ", get_class($exception->getPrevious()), ": ", $exception->getPrevious()->getMessage(), "\n";
    $handled->complete(null);
});
$connection = new Connection();
$scope->spawn(function () {
})->finally(function () use ($connection) {
    throw new LogicException("handler failed");
});
unset($connection);
await(new Future($handled));
echo "end\n";
?>
--EXPECT--
RuntimeException: close failed
previous LogicException: handler failed
end
