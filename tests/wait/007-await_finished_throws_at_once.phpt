--TEST--
S3.7 item 5: await() of a coroutine that already ended by an exception throws at once, before any other queued coroutine runs
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\suspend;

$failed = spawn(function() {
    throw new RuntimeException("already failed");
});

// Queue is [failed, main]: failed runs to its end before main resumes.
suspend();

$other = spawn(function() {
    echo "other runs\n";
});

echo "awaiting failed coroutine\n";
try {
    await($failed);
    echo "no exception\n";
} catch (RuntimeException $e) {
    echo "caught: ", $e->getMessage(), "\n";
}
echo "after await\n";
echo "end of main\n";
?>
--EXPECT--
awaiting failed coroutine
caught: already failed
after await
end of main
other runs
