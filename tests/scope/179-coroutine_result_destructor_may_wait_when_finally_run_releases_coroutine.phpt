--TEST--
Scope: the destructor of a coroutine's result may wait when the coroutine's finally run releases the coroutine
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
$coroutine = Async\spawn(function () use ($closed) {
    return new Connection($closed);
});
$coroutine->finally(function () {
    echo "finally runs\n";
});
unset($coroutine);
await(new Future($closed));
echo "end\n";
?>
--EXPECT--
finally runs
connection closed
end
