<?php
// B14: coroutine_context()->set() and get() in each of <coroutines> coroutines, batched as B1-1000
// (dev/plans/S9-context.md, section 8). Usage: b14.php <batches> <coroutines>. One operation is one
// spawn, its context's set() and get(), and its await; B1-1000 is the same without the context.
use function Async\await;
use function Async\coroutine_context;
use function Async\spawn;

gc_disable();
$batches = (int) ($argv[1] ?? 100);
$size = (int) ($argv[2] ?? 1000);
$task = static function () {
    $context = coroutine_context();
    $context->set('key', 1);

    return $context->get('key');
};
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
