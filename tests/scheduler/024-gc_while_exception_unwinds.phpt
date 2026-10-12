--TEST--
A collection started while an exception unwinds a frame waits for its run and leaves the exception to unwind on
--FILE--
<?php
use function Async\spawn;
use function Async\await;

/* Fills the root buffer past the threshold, which queues a collection, up to one root short of the
 * 100 000 roots past it at which the coroutine waits for that collection. */
function fill_root_buffer(): void
{
    $limit = gc_status()['threshold'] - 1 + 100000;

    while (gc_status()['roots'] < $limit) {
        $object = new stdClass();
        $object->self = $object;
        unset($object);
    }
}

function throw_with_cycle(int &$runs): void
{
    $node = new stdClass();
    $node->self = $node;
    fill_root_buffer();
    $runs = gc_status()['runs'];
    /* Unwinding releases $node: its root makes the coroutine wait for the collection, which runs
     * while the exception is pending. */
    throw new RuntimeException("unwinding");
}

function run(string $where): void
{
    $runs = 0;

    try {
        throw_with_cycle($runs);
    } catch (RuntimeException $exception) {
        $collected = gc_status()['runs'] > $runs ? "a collection ran" : "no collection";
        echo $where, ": caught ", $exception->getMessage(), ", ", $collected, "\n";
    }
}

run("main");
await(spawn(fn() => run("coroutine")));
?>
--EXPECT--
main: caught unwinding, a collection ran
coroutine: caught unwinding, a collection ran
