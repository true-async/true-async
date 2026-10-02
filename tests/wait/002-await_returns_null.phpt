--TEST--
S3.7 item 2: await() of a coroutine whose callable returns nothing or null gives null
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\suspend;

$noReturn = spawn(function() {
});
$bareReturn = spawn(function() {
    return;
});
$nullReturn = spawn(function() {
    return null;
});
$arrowNull = spawn(fn() => null);
$suspendsFirst = spawn(function() {
    suspend();
});

var_dump(await($noReturn));
var_dump(await($bareReturn));
var_dump(await($nullReturn));
var_dump(await($arrowNull));
var_dump(await($suspendsFirst));

echo "end\n";
?>
--EXPECT--
NULL
NULL
NULL
NULL
NULL
end
