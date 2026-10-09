--TEST--
Scope: cancel() of a scope whose member returned by itself leaves the member's waiting finally handler to finish
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$scope = new Async\Scope();
$in_finally = false;
$go = false;
$scope->spawn(function () use (&$in_finally, &$go) {
    Async\current_coroutine()->finally(function () use (&$in_finally, &$go) {
        $in_finally = true;

        while (!$go) {
            suspend();
        }

        echo "finally ends\n";
    });
});

while (!$in_finally) {
    suspend();
}

$scope->cancel();
$go = true;
delay(10);
echo "end\n";
?>
--EXPECT--
finally ends
end
