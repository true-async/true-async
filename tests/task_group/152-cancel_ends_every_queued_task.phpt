--TEST--
TaskGroup: cancel() ends every queued task with the cancellation and releases its call at once
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskGroup;

class Argument
{
    public function __construct(private string $name) {}

    public function __destruct()
    {
        echo "released {$this->name}\n";
    }
}

$group = new TaskGroup(concurrency: 1);
$group->spawn(fn() => (new Future(new FutureState()))->await());
$group->spawn(fn($argument) => "never", new Argument("first"));
$group->spawn(fn($argument) => "never", new Argument("second"));

$group->cancel();
echo "cancelled\n";

foreach ($group->getErrors() as $key => $error) {
    echo "$key: ", get_class($error), "\n";
}
?>
--EXPECT--
released first
released second
cancelled
1: Async\AsyncCancellation
2: Async\AsyncCancellation
