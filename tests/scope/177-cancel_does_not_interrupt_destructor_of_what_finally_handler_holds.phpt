--TEST--
Scope: Scope::cancel() does not interrupt a waiting destructor of what a Coroutine::finally() handler holds
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use function Async\await;

final class Connection
{
    public function __construct(private FutureState $waiting, private Future $goodbye, private FutureState $closed)
    {
    }

    public function __destruct()
    {
        $this->waiting->complete(null);

        try {
            await($this->goodbye);
            echo "connection closed\n";
        } catch (Async\AsyncCancellation) {
            echo "destructor interrupted\n";
        }

        $this->closed->complete(null);
    }
}

$waiting = new FutureState();
$goodbye = new FutureState();
$closed = new FutureState();
$scope = new Async\Scope();
$connection = new Connection($waiting, new Future($goodbye), $closed);
$scope->spawn(function () {
})->finally(function () use ($connection) {
});
unset($connection);
await(new Future($waiting));
$scope->cancel();
$goodbye->complete(null);
await(new Future($closed));
echo "end\n";
?>
--EXPECT--
connection closed
end
