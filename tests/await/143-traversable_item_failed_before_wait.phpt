--TEST--
await_any_or_fail(): a Traversable's item that failed before the wait throws its exception
--FILE--
<?php

use Async\Future;
use function Async\await_any_or_fail;

function items(): Generator
{
    yield Future::failed(new LogicException("failed"));
}

try {
    await_any_or_fail(items());
} catch (LogicException $exception) {
    echo $exception->getMessage(), "\n";
}

$reference = WeakReference::create($exception);
unset($exception);
echo "freed: ", var_export($reference->get() === null, true), "\n";
?>
--EXPECT--
failed
freed: true
