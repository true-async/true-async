--TEST--
An exception nobody observed is thrown where the last reference to the coroutine goes; an observed one is not
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;

$c = spawn(function () { throw new RuntimeException("never observed"); });
suspend();
var_dump($c->isCompleted());
try {
    unset($c);
} catch (RuntimeException $e) {
    echo "caught at release: ", $e->getMessage(), "\n";
}
$d = spawn(function () { throw new RuntimeException("observed"); });
suspend();
try { Async\await($d); } catch (RuntimeException $e) { echo "await: ", $e->getMessage(), "\n"; }
unset($d);
echo "end\n";
?>
--EXPECTF--
bool(true)
caught at release: never observed
await: observed
end
