--TEST--
S3.7 edge case (item 3): await() of a coroutine that ended by an exception thrown in a nested function call
--FILE--
<?php

use function Async\spawn;
use function Async\await;

function inner(): void {
    throw new DomainException("deep");
}

function outer(): void {
    inner();
}

$coroutine = spawn(function() {
    outer();
    return "unreachable";
});

try {
    var_dump(await($coroutine));
} catch (DomainException $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
    var_dump($e === $coroutine->getException());
}

echo "end\n";
?>
--EXPECT--
DomainException: deep
bool(true)
end
