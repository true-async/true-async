<?php
// B16: 4 producers and 1 consumer through a channel of capacity 64 (dev/plans/S9-channel.md,
// section 9). Usage: b16.php <values> [known]. Each producer sends a quarter of the values; one
// operation is one value sent and received. "known" sends a `new stdClass` instead of the counter:
// the known-answer variant.
use Async\Channel;
use function Async\await;
use function Async\spawn;

gc_disable();
$values = (int) ($argv[1] ?? 100000);
$known = ($argv[2] ?? '') === 'known';
$producers = 4;
$share = intdiv($values, $producers);
$channel = new Channel(64);
$coroutines = [];

for ($p = 0; $p < $producers; $p++) {
    $coroutines[] = spawn(static function () use ($channel, $share, $known) {
        for ($i = 0; $i < $share; $i++) {
            $channel->send($known ? new stdClass() : $i);
        }
    });
}

$coroutines[] = spawn(static function () use ($channel, $share, $producers) {
    for ($i = 0, $total = $share * $producers; $i < $total; $i++) {
        $channel->recv();
    }
});

foreach ($coroutines as $coroutine) {
    await($coroutine);
}
