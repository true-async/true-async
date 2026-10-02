--TEST--
S3.7 item 3: await() throws the coroutine's exception, the same object as getException(), at every await()
--FILE--
<?php

use function Async\spawn;
use function Async\await;

$coroutine = spawn(function() {
    throw new RuntimeException("boom");
});

$first = null;
try {
    await($coroutine);
    echo "no exception\n";
} catch (RuntimeException $e) {
    $first = $e;
    echo get_class($e), ": ", $e->getMessage(), "\n";
}

var_dump($first === $coroutine->getException());

try {
    await($coroutine);
    echo "no exception\n";
} catch (RuntimeException $e) {
    echo "second await: ";
    var_dump($e === $first);
}

try {
    await($coroutine);
    echo "no exception\n";
} catch (RuntimeException $e) {
    echo "third await: ";
    var_dump($e === $first);
}

echo "end\n";
?>
--EXPECT--
RuntimeException: boom
bool(true)
second await: bool(true)
third await: bool(true)
end
