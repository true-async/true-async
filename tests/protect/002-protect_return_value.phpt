--TEST--
Async\protect: should return a value
--XFAIL--
Not implemented yet: S3.8 of dev/PLAN.md
--FILE--
<?php

use function Async\protect;

$result = protect(function() {
    return "test value";
});

var_dump($result);

?>
--EXPECT--
string(10) "test value"