--TEST--
Context: a finished coroutine whose context holds the coroutine is collected by gc_collect_cycles()
--FILE--
<?php

use function Async\await;
use function Async\spawn;

class Marker
{
    public function __destruct()
    {
        echo "marker freed\n";
    }
}

$coroutine = spawn(fn() => 1);
await($coroutine);
$coroutine->getContext()->set('self', $coroutine);
$coroutine->getContext()->set('marker', new Marker());
$weak = WeakReference::create($coroutine);
unset($coroutine);

$collected = gc_collect_cycles();
echo "collected: ", var_export($collected > 0, true), "\n";
var_dump($weak->get());

?>
--EXPECT--
marker freed
collected: true
NULL
