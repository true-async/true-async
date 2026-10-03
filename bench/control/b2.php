<?php
// B2 on ext/test_scheduler, the RFC core's empty scheduler (D2's control): ping-pong of two
// coroutines (dev/plans/S3.md, section 12). Its suspend() parks until a resume(), so each player
// resumes the other before it parks. Usage: b2.php <suspends>. One operation is one suspend() with
// its switch and the resume() that ends it.
use function TestScheduler\spawn;
use function TestScheduler\await;
use function TestScheduler\resume;
use function TestScheduler\suspend;

$half = intdiv((int) ($argv[1] ?? 2000000), 2);
$players = [];
$player = static function (int $index) use (&$players, $half) {
    $other = $players[1 - $index];

    for ($i = 0; $i < $half; $i++) {
        if ($other->isSuspended()) {
            resume($other);
        }

        suspend();
    }

    if ($other->isSuspended()) {
        resume($other);
    }
};
$players[0] = spawn($player, 0);
$players[1] = spawn($player, 1);
await($players[0]);
await($players[1]);
