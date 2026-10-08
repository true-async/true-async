--TEST--
Context: set() replacing the value of an object key holds the key object once, and unset() releases it
--FILE--
<?php

$key = new stdClass();
$weak_key = WeakReference::create($key);

$context = new Async\Context();
$context->set($key, 'first');
$context->set($key, 'second', true);
var_dump($context->get($key));

unset($key);
var_dump($weak_key->get() !== null);

$context->unset($weak_key->get());
var_dump($weak_key->get());

?>
--EXPECT--
string(6) "second"
bool(true)
NULL
