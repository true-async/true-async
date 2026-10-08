--TEST--
Context: a cycle through an object key (the key object holds the Context) is collected by gc_collect_cycles()
--FILE--
<?php

class Key
{
    public ?Async\Context $context = null;

    public function __destruct()
    {
        echo "key freed\n";
    }
}

$key = new Key();
$key->context = new Async\Context();
$key->context->set($key, 'value');
$weak = WeakReference::create($key->context);
unset($key);

$collected = gc_collect_cycles();
echo "collected: ", var_export($collected > 0, true), "\n";
var_dump($weak->get());

?>
--EXPECT--
key freed
collected: true
NULL
