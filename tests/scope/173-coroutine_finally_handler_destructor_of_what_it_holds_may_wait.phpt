--TEST--
Scope: the destructor of what a Coroutine::finally() handler holds may wait when the run releases the handler
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
$connection = new Connection($closed);
$coroutine = Async\spawn(function () {
});
$coroutine->finally(function () use ($connection) {
    echo "finally runs\n";
});
unset($connection);
await(new Future($closed));
echo "end\n";
?>
--EXPECT--
finally runs
connection closed
end
