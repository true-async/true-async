<?php
// B2: ping-pong of two coroutines (dev/plans/S3.md, section 12).
// Usage: b2.php <suspends>. One operation is one suspend() with its switch.
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$half = intdiv((int) ($argv[1] ?? 2000000), 2);
$player = static function () use ($half) {
    for ($i = 0; $i < $half; $i++) {
        suspend();
    }
};
$first = spawn($player);
$second = spawn($player);
await($first);
await($second);
