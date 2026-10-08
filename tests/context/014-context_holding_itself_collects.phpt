--TEST--
Context: a Context that holds itself is collected by gc_collect_cycles()
--FILE--
<?php

class Marker
{
    public function __destruct()
    {
        echo "marker freed\n";
    }
}

$context = new Async\Context();
$context->set('self', $context);
$context->set('marker', new Marker());
$weak = WeakReference::create($context);
unset($context);

$collected = gc_collect_cycles();
echo "collected: ", var_export($collected > 0, true), "\n";
var_dump($weak->get());

?>
--EXPECT--
marker freed
collected: true
NULL
