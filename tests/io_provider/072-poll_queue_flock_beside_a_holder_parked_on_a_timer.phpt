--TEST--
On the Poll queue, a contended flock() parks its coroutine while the holder waits on a timer, and takes the lock once the holder unlocks (review M10, dev/RFC-CHANGES.md 11)
--FILE--
<?php
TrueAsync\Test\reactor_use_poll_queue();
use function Async\await;
use function Async\delay;
use function Async\spawn;

$file = __DIR__ . '/072-flock.tmp';
touch($file);
$holder_end = fopen($file, 'r');
flock($holder_end, LOCK_EX);

$locker = spawn(function () use ($file) {
    $locker_end = fopen($file, 'r');
    flock($locker_end, LOCK_EX);
    echo "locker: acquired\n";
    fclose($locker_end);
});

$other = spawn(function () {
    echo "other: running\n";
});

delay(20);
echo "holder: unlocking\n";
flock($holder_end, LOCK_UN);
fclose($holder_end);

await($locker);
await($other);
?>
--CLEAN--
<?php
@unlink(__DIR__ . '/072-flock.tmp');
?>
--EXPECT--
other: running
holder: unlocking
locker: acquired
