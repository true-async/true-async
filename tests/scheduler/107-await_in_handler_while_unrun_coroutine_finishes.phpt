--TEST--
Main parked inside an opcode handler pops a cancelled, never run coroutine: its finalize on main's stack leaves main's frame where it was
--FILE--
<?php
use function Async\await;
use function Async\spawn;

$coroutine = spawn(function () {
    echo "body\n";
});
$target = spawn(function () {
    echo "target\n";
});
$coroutine->cancel();

class WaitsInDestructor
{
    public function __destruct()
    {
        global $target;
        await($target);
        echo "destructor\n";
    }
}

/* The assignment runs the destructor inside its handler; main parks there, and its suspend pops the
 * cancelled coroutine, queued first. */
$object = new WaitsInDestructor();
$object = null;

var_dump($object, $coroutine->isCancelled());
echo "main end\n";
?>
--EXPECT--
target
destructor
NULL
bool(true)
main end
