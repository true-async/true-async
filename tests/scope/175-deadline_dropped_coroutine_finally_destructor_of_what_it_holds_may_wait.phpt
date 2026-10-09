--TEST--
Scope: the destructor of what a Coroutine::finally() handler holds may wait when the disposeAfterTimeout() fire keeps the handler from being called
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\delay;

final class Connection
{
    public function __construct(private FutureState $closed)
    {
    }

    public function __destruct()
    {
        delay(1);
        echo "connection closed\n";
        $this->closed->complete(null);
    }
}

$closed = new FutureState();
$started = new FutureState();
$blocker = new FutureState();
$scope = new Async\Scope();
$scope->spawn(function () use ($started, $closed, $blocker) {
    $connection = new Connection($closed);
    Async\current_coroutine()->finally(function () use ($connection) {
        echo "finally runs\n";
    });
    unset($connection);
    $started->complete(null);
    await(new Future($blocker));
});
await(new Future($started));
$scope->disposeAfterTimeout(10);
await(new Future($closed));
echo "end\n";
?>
--EXPECT--
connection closed
end
