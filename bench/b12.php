<?php
// B12: Scope::awaitCompletion() over N members (dev/plans/S9-scope.md, section 10). Usage: b12.php
// <members> <rounds>. Each round spawns <members> coroutines into a new Scope and waits for them with
// one awaitCompletion(); one operation is one member.
use Async\Scope;
use Async\Future;
use Async\FutureState;

gc_disable();
$members = (int) ($argv[1] ?? 1000);
$rounds = (int) ($argv[2] ?? 10);
$task = static function () { return 1; };
$token = new Future(new FutureState());

for ($round = 0; $round < $rounds; $round++) {
    $scope = new Scope();

    for ($i = 0; $i < $members; $i++) {
        $scope->spawn($task);
    }

    $scope->awaitCompletion($token);
}
