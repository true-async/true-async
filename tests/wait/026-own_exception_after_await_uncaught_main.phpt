--TEST--
S3.7 edge case: an uncaught exception main throws itself after await() returned ends the request as uncaught
--FILE--
<?php

use function Async\spawn;
use function Async\await;

$target = spawn(function() {
    return "fine";
});

var_dump(await($target));
throw new LogicException("own failure");
?>
--EXPECTF--
string(4) "fine"
%AFatal error: Uncaught LogicException: own failure in %s:%d
%A
