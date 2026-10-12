--TEST--
A collection that starts inside an opcode handler parks main while a cancelled, never run coroutine finishes: main's frame stays where it was
--FILE--
<?php
use function Async\spawn;

$coroutine = spawn(function () {
    echo "body\n";
});
$coroutine->cancel();

/* The assignment that passes the GC threshold queues the collection; the one 100 000 roots past it
 * makes main wait for it inside its handler while the cancelled coroutine finishes. */
$status = gc_status();
$keep = [];
$object = null;

for ($i = $status['roots'] - 3; $i < $status['threshold'] + 100000; $i++) {
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
