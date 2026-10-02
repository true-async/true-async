--TEST--
A collection started while an exception unwinds a frame waits for its run and leaves the exception to unwind on
--FILE--
<?php
use function Async\spawn;
use function Async\await;

/* Fills the root buffer up to one short of a collection; the next possible root starts one. */
function fill_root_buffer(): void
{
    $threshold = gc_status()['threshold'];

    while (gc_status()['roots'] < $threshold - 1) {
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
    /* Unwinding releases $node: its root fills the buffer, and the collection runs while the
     * exception is pending. */
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
