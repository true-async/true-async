--TEST--
A coroutine cancelled before it ran finishes without running its body when the scheduler reaches it, and every await() throws the cancellation
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use Async\AsyncCancellation;

$c = spawn(function () { echo "not run\n"; });
$c->cancel();
var_dump($c->isCancellationRequested(), $c->isCancelled(), $c->isStarted(), $c->isCompleted());
try { await($c); } catch (AsyncCancellation $e) { echo "await: ", $e->getMessage(), "\n"; }
var_dump($c->isCancelled(), $c->isStarted(), $c->isCompleted());
try { await($c); } catch (AsyncCancellation $e) { echo "again: ", $e->getMessage(), "\n"; }
echo "end\n";
?>
--EXPECTF--
bool(true)
bool(false)
bool(false)
bool(false)
await: Coroutine cancelled
bool(true)
bool(false)
bool(true)
again: Coroutine cancelled
end
