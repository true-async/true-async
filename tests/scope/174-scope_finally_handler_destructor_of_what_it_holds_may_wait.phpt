--TEST--
Scope: the destructor of what a Scope::finally() handler holds may wait when the run releases the handler
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
$scope = new Async\Scope();
$connection = new Connection($closed);
$scope->finally(function () use ($connection) {
    echo "scope finally runs\n";
});
unset($connection);
await($scope->spawn(function () {
}));
$scope->dispose();
await(new Future($closed));
echo "end\n";
?>
--EXPECT--
scope finally runs
connection closed
end
