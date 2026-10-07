<?php
// B10: Future::map() chains (dev/plans/S5.md, "Measurements"). Usage: b10.php <depth|fanout> <size>
// <rounds>. "depth" maps one Future <size> times in a line, "fanout" maps one Future <size> times;
// the root is completed after the chain is built and the leaves awaited. One operation is one mapper.
use Async\Future;
use Async\FutureState;
use function Async\await_all;

$mode = $argv[1] ?? 'depth';
$size = (int) ($argv[2] ?? 1000);
$rounds = (int) ($argv[3] ?? 10);
$increment = static fn(int $value): int => $value + 1;

for ($round = 0; $round < $rounds; $round++) {
    $state = new FutureState();
    $root = new Future($state);
    $leaves = [];

    if ($mode === 'depth') {
        $leaf = $root;

        for ($i = 0; $i < $size; $i++) {
            $leaf = $leaf->map($increment);
        }

        $leaves[] = $leaf;
    } else {
        for ($i = 0; $i < $size; $i++) {
            $leaves[] = $root->map($increment);
        }
    }

    $state->complete(0);
    await_all($leaves);
}
