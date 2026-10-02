--TEST--
S3.7 item 5: await() of a finished coroutine returns at once, before any other queued coroutine runs
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\suspend;

$done = spawn(function() {
    return "early";
});

// Queue is [done, main]: done runs to its end before main resumes.
suspend();

$other = spawn(function() {
    echo "other runs\n";
});

echo "awaiting finished coroutine\n";
var_dump(await($done));
echo "after await\n";
var_dump(await($done));
echo "end of main\n";
?>
--EXPECT--
awaiting finished coroutine
string(5) "early"
after await
string(5) "early"
end of main
other runs
