--TEST--
Scope: a Coroutine::finally() handler's error goes to the scope's exception handler when the disposeAfterTimeout() fire interrupts a waiting destructor of what the handler holds
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use function Async\await;

final class Connection
{
    public function __construct(private FutureState $waiting, private Future $never)
    {
    }

    public function __destruct()
    {
        $this->waiting->complete(null);
        await($this->never);
    }
}

$waiting = new FutureState();
$handled = new FutureState();
$scope = new Async\Scope();
$scope->setExceptionHandler(function ($scope, $coroutine, $exception) use ($handled) {
    echo get_class($exception), ": ", $exception->getMessage(), "\n";
    $handled->complete(null);
});
$blocker = new FutureState();
$connection = new Connection($waiting, new Future($blocker));
$scope->spawn(function () {
})->finally(function () use ($connection) {
    throw new LogicException("handler failed");
});
unset($connection);
await(new Future($waiting));
$scope->disposeAfterTimeout(10);
await(new Future($handled));
echo "end\n";
?>
--EXPECT--
LogicException: handler failed
end
