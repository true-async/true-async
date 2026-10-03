--TEST--
A coroutine spawned in the request's shutdown destructors throws while a collection, with no frame on main's stack, waits: only its own uncaught exception is reported
--FILE--
<?php
class Thrower
{
    public function __destruct()
    {
        Async\spawn(function () {
            throw new Exception("boom in shutdown");
        });
        echo "thrower done\n";
    }
}

/* Globals go in reverse order: the thrower first, then the holder, whose release fills the GC's
 * root buffer and starts a collection that main waits for (scheduler/074). */
$holder = new stdClass();
$holder->items = [];
$all = [];

for ($i = 0; $i < 20000; $i++) {
    $object = new stdClass();
    $all[] = $object;
    $holder->items[] = $object;
}

$thrower = new Thrower();
echo "main end\n";
?>
--EXPECTF--
main end
thrower done

Fatal error: Uncaught Exception: boom in shutdown in %s:%d
Stack trace:
#0 [internal function]: Thrower->{closure:Thrower::__destruct():6}()
#1 {main}
  thrown in %s on line %d
