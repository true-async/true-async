--TEST--
TaskSet: a destructor run by joinAll()'s release of the entries it took finds the set empty
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\TaskSet;

final class ReadsOnDestruct
{
    public function __construct(private TaskSet $set)
    {
    }

    public function __destruct()
    {
        echo "destructor: count ", count($this->set), "\n";
        var_dump($this->set->joinAll()->await());
    }
}

final class Failure extends RuntimeException
{
    public ?ReadsOnDestruct $hook = null;
}

$failing = new FutureState();
$set = new TaskSet();
$set->spawn(fn() => "first");
$set->spawn(function () use ($set, $failing) {
    $error = new Failure("failed");
    $error->hook = new ReadsOnDestruct($set);
    $failing->complete(null);
    throw $error;
});

(new Future($failing))->await();
var_dump($set->joinAll(ignoreErrors: true)->await());
echo "end\n";
?>
--EXPECT--
destructor: count 0
array(0) {
}
array(1) {
  [0]=>
  string(5) "first"
}
end
