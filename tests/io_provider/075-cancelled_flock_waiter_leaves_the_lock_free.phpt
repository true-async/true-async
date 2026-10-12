--TEST--
A flock() waiter cancelled while the holder waits for it takes no lock, and the lock is free once the holder unlocks (review M10, dev/RFC-CHANGES.md 11)
--FILE--
<?php
use function Async\await;
use function Async\spawn;
use function Async\suspend;

$file = __DIR__ . '/075-flock.tmp';
touch($file);
$holder_end = fopen($file, 'r');
flock($holder_end, LOCK_EX);

$locker_end = null;
$waiting = false;

// The locker's handle outlives it: a lock it took despite the cancellation would stay held.
$locker = spawn(function () use ($file, &$locker_end, &$waiting) {
    $locker_end = fopen($file, 'r');
    $waiting = true;
    flock($locker_end, LOCK_EX);
    echo "locker: acquired\n";
});

while (!$waiting) {
    suspend();
}
$locker->cancel();

try {
    await($locker);
} catch (Async\AsyncCancellation $e) {
    echo "locker: cancelled\n";
}

flock($holder_end, LOCK_UN);
$probe_end = fopen($file, 'r');
echo "lock free: ", var_export(flock($probe_end, LOCK_EX | LOCK_NB), true), "\n";
?>
--CLEAN--
<?php
@unlink(__DIR__ . '/075-flock.tmp');
?>
--EXPECT--
locker: cancelled
lock free: true
