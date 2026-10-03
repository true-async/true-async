--TEST--
spawn() keeps the object a class-string callable resolved to: $this of the spawning method outlives it for the coroutine, through a method and through __call, and goes when the run ends or the unrun coroutine is released
--FILE--
<?php
class Target
{
    public string $label = "kept";

    public function method()
    {
        echo "method: {$this->label}\n";
    }

    public function __call($name, $arguments)
    {
        echo "__call $name: {$this->label}\n";
    }

    public function spawnMethod()
    {
        return Async\spawn([Target::class, 'method']);
    }

    public function spawnMagic()
    {
        return Async\spawn([Target::class, 'magic']);
    }

    public function __destruct()
    {
        echo "released: {$this->label}\n";
    }
}

/* The only reference to each Target is the spawning call's $this. */
$first = (new Target)->spawnMethod();
$second = (new Target)->spawnMagic();
$filler = array_fill(0, 100, new stdClass());

Async\await($first);
Async\await($second);
echo "awaited\n";

/* A coroutine that never runs drops the object and the __call trampoline with its release, when
 * its turn in the queue finishes it after main. */
$unrun = (new Target)->spawnMagic();
$unrun->cancel();
unset($unrun);
echo "done\n";
?>
--EXPECT--
method: kept
released: kept
__call magic: kept
released: kept
awaited
done
released: kept
