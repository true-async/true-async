--TEST--
A graceful shutdown's cancelling walk starts a collection that adds the GC coroutine to the coroutine table, full at that moment: the walk takes only the coroutines it started with, and the collection runs from the queue before the drain ends
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;

class Node
{
    public static int $destructed = 0;
    public $other;

    public function __destruct()
    {
        self::$destructed++;
    }
}

register_shutdown_function(function () {
    echo "shutdown function: ", Node::$destructed, " nodes destructed, ", gc_status()['runs'] > 0 ? "collected" : "not collected", "\n";
});

/* Ten garbage cycles with destructors: the collection the walk starts destructs them. */
for ($i = 0; $i < 10; $i++) {
    $first = new Node();
    $second = new Node();
    $first->other = $second;
    $second->other = $first;
    unset($first, $second);
}

/* Main and seven coroutines fill the table of eight; the GC coroutine is the ninth. */
$coroutines = [];

for ($i = 0; $i < 7; $i++) {
    $coroutines[] = spawn(function () {
        try {
            suspend();
        } catch (Async\AsyncCancellation $e) {
            echo "cancelled\n";
        }
    });
}

/* Every coroutine parks in its suspend(). */
suspend();

/* The walk drops the shared cancellation for this one, whose own is pending: that release adds the
 * root that fills the GC buffer. */
$coroutines[0]->cancel();

$status = gc_status();
$keep = [];
$object = null;

for ($i = $status['roots']; $i < $status['threshold']; $i++) {
    $object = new stdClass();
    $keep[] = $object;
}

Async\graceful_shutdown();
echo "main end\n";
?>
--EXPECT--
main end
cancelled
cancelled
cancelled
cancelled
cancelled
cancelled
cancelled
shutdown function: 20 nodes destructed, collected
