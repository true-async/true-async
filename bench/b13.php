<?php
// B13: Context::find() of a missing key from a scope <depth> levels below the root scope
// (dev/plans/S9-context.md, section 8). Usage: b13.php <depth> <calls>. Every scope on the way has a
// context with one key, so the walk reads each level; one operation is one find().
use Async\Scope;
use function Async\await;
use function Async\current_context;

gc_disable();
$depth = (int) ($argv[1] ?? 10);
$calls = (int) ($argv[2] ?? 100000);
$scopes = [];
$scope = Scope::inherit();

for ($level = 0; $level < $depth; $level++) {
    $scopes[] = $scope;
    await($scope->spawn(static fn() => current_context()->set('level', 1)));
    $scope = Scope::inherit($scope);
}

await($scope->spawn(static function () use ($calls) {
    $context = current_context();

    for ($i = 0; $i < $calls; $i++) {
        $context->find('missing');
    }
}));
