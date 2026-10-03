<?php
// B1: spawn and await in batches of 100 (dev/plans/S3.md, section 12).
// Usage: b1.php <batches> [batch size] [known]. One operation is one spawn and its await.
// "known" adds one `new stdClass` per spawn: the known-answer variant.
use function Async\spawn;
use function Async\await;

gc_disable();
$batches = (int) ($argv[1] ?? 1000);
$size = (int) ($argv[2] ?? 100);
$known = ($argv[3] ?? '') === 'known';
$task = $known ? static function () { return new stdClass(); } : static function () { return 1; };
$coroutines = [];

for ($batch = 0; $batch < $batches; $batch++) {
    for ($i = 0; $i < $size; $i++) {
        $coroutines[$i] = spawn($task);
    }

    for ($i = 0; $i < $size; $i++) {
        await($coroutines[$i]);
    }

    $coroutines = [];
}
