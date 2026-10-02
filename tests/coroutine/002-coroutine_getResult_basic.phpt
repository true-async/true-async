--TEST--
Coroutine: getResult() - basic usage
--XFAIL--
Not implemented yet: S3.7 of dev/PLAN.md
--FILE--
<?php

use function Async\spawn;
use function Async\await;

$coroutine = spawn(function() {
    return "test result";
});

// Before completion
var_dump($coroutine->getResult());

// After completion
await($coroutine);
var_dump($coroutine->getResult());

?>
--EXPECT--
NULL
string(11) "test result"