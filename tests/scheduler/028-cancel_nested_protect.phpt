--TEST--
A cancellation of a coroutine inside nested protect() is deferred until the outermost protect() returns, and await() sees it
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\protect;
use function Async\suspend;
use Async\AsyncCancellation;

$c = spawn(function () {
    protect(function () {
        protect(function () {
            suspend();
            echo "inner done\n";
        });
        echo "outer still protected\n";
        suspend();
        echo "outer done\n";
    });
    echo "not reached\n";
});
suspend();
$c->cancel(new AsyncCancellation("stop"));
echo "cancel requested\n";
try { await($c); } catch (AsyncCancellation $e) { echo "await: ", $e->getMessage(), "\n"; }
var_dump($c->isCancelled());
?>
--EXPECTF--
cancel requested
inner done
outer still protected
outer done
await: stop
bool(true)
