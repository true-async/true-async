<?php
// B21: acquire()/release() pairs from 10 coroutines over a Pool (dev/plans/S9-pool.md, section 10).
// Usage: b21.php <pairs> [max] [known]. 10 coroutines; with max 2 (the default) eight of them wait in
// acquire() at any time, with max 10 nobody waits (B22). One operation is one pair with its yield. "known"
// adds a `new stdClass` per pair: the known-answer variant.
use Async\Pool;
use function Async\spawn;
use function Async\await;

gc_disable();
$pairs = (int) ($argv[1] ?? 100000);
$max = (int) ($argv[2] ?? 2);
$known = ($argv[3] ?? '') === 'known';
$pool = new Pool(factory: static fn() => new stdClass(), max: $max);
$coroutines = [];

for ($i = 0; $i < 10; $i++) {
    $coroutines[] = spawn(static function () use ($pool, $pairs, $known) {
        for ($j = $pairs / 10; $j > 0; $j--) {
            $resource = $pool->acquire();
            $extra = $known ? new stdClass() : null;
            Async\suspend();
            $pool->release($resource);
        }
    });
}

foreach ($coroutines as $coroutine) {
    await($coroutine);
}
