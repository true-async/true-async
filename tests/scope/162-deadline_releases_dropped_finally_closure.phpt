--TEST--
Scope: a Coroutine::finally() handler that the disposeAfterTimeout() fire keeps from being called is released with what it holds
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

final class Resource
{
    public function __destruct()
    {
        echo "resource released\n";
    }
}

$scope = new Async\Scope();
$started = false;
$resource = new Resource();
$scope->spawn(function () use (&$started, $resource) {
    Async\current_coroutine()->finally(function () use ($resource) {
    });
    $started = true;
    delay(100000);
});
unset($resource);

while (!$started) {
    suspend();
}

$scope->disposeAfterTimeout(10);
delay(60);
echo "end\n";
?>
--EXPECT--
resource released
end
