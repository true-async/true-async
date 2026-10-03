--TEST--
A collection started by a microtask in the tick after a coroutine's end is deferred, as everywhere in scheduler context: it does not run its destructors inline with no current coroutine
--FILE--
<?php
use function Async\spawn;
use function Async\await;

class Cyclic
{
    public $self;

    public function __destruct()
    {
        echo "destructor\n";
    }
}

$coroutine = spawn(function () {
    TrueAsync\Test\defer('a', null, function () {
        $cyclic = new Cyclic();
        $cyclic->self = $cyclic;
        unset($cyclic);
        echo "collected: ", gc_collect_cycles(), "\n";
    });
    echo "coroutine end\n";
});

await($coroutine);
echo "main end\n";
?>
--EXPECT--
coroutine end
microtask a sched=1
collected: 0
released a
main end
destructor
