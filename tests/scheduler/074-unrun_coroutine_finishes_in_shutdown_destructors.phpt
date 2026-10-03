--TEST--
A collection started in the request's shutdown destructors, with no frame on main's stack, pops a coroutine cancelled before it ran: it finishes with its cancellation and nothing is reported
--FILE--
<?php
class Spawner
{
    public function __destruct()
    {
        Async\spawn(function () {})->cancel();
        echo "spawner done\n";
    }
}

/* Globals go in reverse order: the spawner first, then the holder, whose release fills the GC's
 * root buffer and starts a collection that main waits for. */
$holder = new stdClass();
$holder->items = [];
$all = [];

for ($i = 0; $i < 20000; $i++) {
    $object = new stdClass();
    $all[] = $object;
    $holder->items[] = $object;
}

$spawner = new Spawner();
echo "main end\n";
?>
--EXPECT--
main end
spawner done
