--TEST--
A coroutine cancelled before it ran is finished outside scheduler context, whether main's suspend() or the scheduler pops it
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;
use function Async\current_coroutine;

class Probe
{
    public function __construct(private string $name) {}

    public function __destruct()
    {
        try {
            echo $this->name, ": ", var_export(current_coroutine()->isCancelled(), true), "\n";
        } catch (Error $e) {
            echo $this->name, ": ", $e->getMessage(), "\n";
        }
    }
}

/* Popped by main's suspend(). */
spawn(function ($probe) {}, new Probe("popped in main"))->cancel();
suspend();

/* Popped by the scheduler coroutine after main. */
spawn(function ($probe) {}, new Probe("popped by the scheduler"))->cancel();
echo "main end\n";
?>
--EXPECTF--
popped in main: true
main end
popped by the scheduler: true
