--TEST--
Context: each coroutine has one context of its own, the same object through coroutine_context() and getContext()
--FILE--
<?php

use function Async\await;
use function Async\coroutine_context;
use function Async\current_coroutine;
use function Async\spawn;

coroutine_context()->set('key', 'main');

$child = spawn(function () {
    $context = coroutine_context();
    echo "same object: ", var_export($context === current_coroutine()->getContext(), true), "\n";
    echo "child sees main's key: ", var_export($context->has('key'), true), "\n";
    $context->set('key', 'child');
});
await($child);

echo "main's value: ", coroutine_context()->get('key'), "\n";
echo "child's value: ", $child->getContext()->get('key'), "\n";
echo "same object twice: ", var_export($child->getContext() === $child->getContext(), true), "\n";
echo "main's is not the child's: ", var_export(coroutine_context() !== $child->getContext(), true), "\n";

?>
--EXPECT--
same object: true
child sees main's key: false
main's value: main
child's value: child
same object twice: true
main's is not the child's: true
