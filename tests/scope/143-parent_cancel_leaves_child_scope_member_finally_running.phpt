--TEST--
Scope: the parent's cancel() leaves the waiting finally handler of a child scope's member that returned by itself to finish
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$parent = new Async\Scope();
$child = Async\Scope::inherit($parent);
$in_finally = false;
$go = false;
$child->spawn(function () use (&$in_finally, &$go) {
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

$parent->cancel();
$go = true;
delay(10);
echo "end\n";
?>
--EXPECT--
finally ends
end
