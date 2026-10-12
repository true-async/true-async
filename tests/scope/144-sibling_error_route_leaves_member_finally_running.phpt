--TEST--
Scope: a sibling's unhandled error, which cancels the scope, leaves a returned member's waiting finally handler to finish
--FILE--
<?php

use function Async\suspend;

$scope = new Async\Scope();
$in_finally = false;
$go = false;
$ended = false;
$scope->spawn(function () use (&$in_finally, &$go) {
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

$failing = $scope->spawn(function () {
    throw new Exception('sibling fails');
});

while (!$scope->isCancelled()) {
    suspend();
}

$go = true;

// A delay could end before the finally handler has run on: a timer due when main parks wakes it in place
while (!$ended) {
    suspend();
}

try {
    Async\await($failing);
} catch (Exception $exception) {
    echo "observed: ", $exception->getMessage(), "\n";
}

echo "end\n";
?>
--EXPECT--
finally ends
observed: sibling fails
end
