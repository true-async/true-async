--TEST--
A collection that the release of a cancelled, never run coroutine's outcome starts does not park that coroutine, which has no stack: the collection is deferred
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;

$coroutine = spawn(function () {
    echo "body\n";
});
$coroutine->cancel();

/* The GC buffer is one root short of full when the yield below pops the cancelled coroutine: the
 * release of its cancellation in its finalize adds that root. */
$status = gc_status();
$keep = [];
$object = null;

for ($i = $status['roots']; $i < $status['threshold']; $i++) {
    $object = new stdClass();
    $keep[] = $object;
}

suspend();
var_dump($coroutine->isCancelled());
echo "main end\n";
?>
--EXPECT--
bool(true)
main end
