--TEST--
Scope: the parent's cancel() leaves the waiting finally handler of a child scope's member that returned by itself to finish
--FILE--
<?php

use function Async\suspend;

$parent = new Async\Scope();
$child = Async\Scope::inherit($parent);
$in_finally = false;
$go = false;
$ended = false;
$child->spawn(function () use (&$in_finally, &$go) {
    Async\current_coroutine()->finally(function () use (&$in_finally, &$go) {
        $in_finally = true;

        while (!$go) {
            suspend();
        }

        echo "finally ends\n";
        $GLOBALS['ended'] = true;
    });
});

while (!$in_finally) {
    suspend();
}

$parent->cancel();
$go = true;

// A delay could end before the finally handler has run on: a timer due when main parks wakes it in place
while (!$ended) {
    suspend();
}

echo "end\n";
?>
--EXPECT--
finally ends
end
