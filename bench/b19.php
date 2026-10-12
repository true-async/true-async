<?php
// B19: a member of the innermost of N nested scopes finishes, then the chain is released innermost
// first (scope/124's claim, dev/SECURITY.md S9.8). Usage: b19.php <depth>. Each parent walks only its
// other child scopes for completion, so the work is linear in the depth. One operation is one level.
use Async\Scope;
use function Async\await;

gc_disable();
$depth = (int) ($argv[1] ?? 10000);
$scope = Scope::inherit();
$scopes = [$scope];

for ($i = 0; $i < $depth; $i++) {
    $scopes[] = $scope = Scope::inherit($scope);
}

await($scope->spawn(static fn() => 1));
unset($scope);

while ($scopes) {
    array_pop($scopes);
}
