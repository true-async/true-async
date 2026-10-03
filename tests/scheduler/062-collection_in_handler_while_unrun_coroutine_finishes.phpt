--TEST--
Main parked by a collection that starts inside an opcode handler pops a cancelled, never run coroutine: its finalize on main's stack leaves main's frame where it was
--FILE--
<?php
use function Async\spawn;

$coroutine = spawn(function () {
    echo "body\n";
});
$coroutine->cancel();

/* The assignment that passes the GC threshold starts the collection inside its handler; main waits
 * for it, and the cancelled coroutine finishes first. */
$status = gc_status();
$keep = [];
$object = null;

for ($i = $status['roots'] - 3; $i < $status['threshold']; $i++) {
    $object = new stdClass();
    $keep[] = $object;
}

var_dump(count($keep) > 0, $coroutine->isCancelled());
echo "main end\n";
?>
--EXPECT--
bool(true)
bool(true)
main end
