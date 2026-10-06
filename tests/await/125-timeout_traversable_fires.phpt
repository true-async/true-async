--TEST--
await_all() over a Traversable whose items never complete ends at the Timeout's deadline
--FILE--
<?php

use Async\FutureState;
use Async\Future;
use Async\OperationCanceledException;
use function Async\await_all;
use function Async\timeout;

$never = new FutureState();
$never->ignore();

function items(FutureState $state): Generator
{
    yield "a" => new Future($state);
    yield "b" => new Future($state);
}

$start = hrtime(true);

try {
    await_all(items($never), timeout(30));
} catch (OperationCanceledException $e) {
    echo $e->getPrevious()->getMessage(), "\n";
}

$elapsed = (hrtime(true) - $start) / 1e6;
var_dump($elapsed >= 25 && $elapsed < 5000);

?>
--EXPECT--
Timeout occurred after 30 milliseconds
bool(true)
