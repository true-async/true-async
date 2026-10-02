--TEST--
Coroutine: getAwaitingInfo() - basic usage
--XFAIL--
Not implemented yet: S3.7 of dev/PLAN.md
--FILE--
<?php

use function Async\spawn;

$coroutine = spawn(function() {
    return "test";
});

$info = $coroutine->getAwaitingInfo();

var_dump(is_array($info));

?>
--EXPECT--
bool(true)