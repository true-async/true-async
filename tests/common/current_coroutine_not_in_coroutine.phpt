--TEST--
Async\current_coroutine(): the script root runs in the main coroutine, so nothing throws
--XFAIL--
Not implemented yet: S3.5 of dev/PLAN.md
--FILE--
<?php

use function Async\current_coroutine;

// The RFC core starts the scheduler with the script, so the root already runs in the main
// coroutine and current_coroutine() returns it (the reference threw here).

try {
    current_coroutine();
    echo "no-throw\n";
} catch (\Throwable $e) {
    echo "caught: ", $e->getMessage(), "\n";
}

?>
--EXPECT--
no-throw
