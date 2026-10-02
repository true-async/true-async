--TEST--
Future: spawn() - fatal error handling in coroutine
--XFAIL--
Not implemented yet: S3.8 of dev/PLAN.md
--FILE--
<?php

use function Async\spawn;

echo "start\n";

spawn(function() {
    echo "coroutine start\n";
    $obj = new stdClass();
    $obj->undefinedMethod();
    echo "coroutine end (should not print)\n";
});

echo "end\n";
?>
--EXPECTF--
start
end
coroutine start

Fatal error: Uncaught Error: Call to undefined method stdClass::undefinedMethod() in %s:%d
Stack trace:
%a