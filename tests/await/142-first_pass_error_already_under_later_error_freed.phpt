--TEST--
await_any_or_fail(): a failed item whose exception is already the previous of a later item's exception is thrown under it, and freed with it
--FILE--
<?php

use function Async\await;
use function Async\await_any_or_fail;
use function Async\spawn;
use function Async\suspend;

$second_awaits = false;
$first = spawn(function () use (&$second_awaits) {
    while (!$second_awaits) {
        suspend();
    }

    throw new RuntimeException("first");
});
$second = spawn(function () use ($first, &$second_awaits) {
    $second_awaits = true;

    try {
        await($first);
    } catch (RuntimeException $exception) {
        throw new LogicException("second", 0, $exception);
    }
});

try {
    await($second);
} catch (LogicException) {
}

$reference = WeakReference::create($first->getException());

try {
    await_any_or_fail([$first, $second]);
} catch (LogicException $exception) {
    echo get_class($exception), " under ", get_class($exception->getPrevious()), "\n";
}

unset($exception, $first, $second);
echo "first freed: ", var_export($reference->get() === null, true), "\n";
?>
--EXPECT--
LogicException under RuntimeException
first freed: true
