--TEST--
Scope: members of the innermost of 200 nested scopes finish and every scope of the chain is finished; the chain is released innermost first
--FILE--
<?php

use Async\Scope;
use function Async\await;

$scope = Scope::inherit();
$scopes = [$scope];

// The time linear in the depth is benchmark B19 (bench/b19.php), not a test.
for ($i = 0; $i < 200; $i++) {
    $scopes[] = $scope = Scope::inherit($scope);
}

for ($i = 0; $i < 100; $i++) {
    await($scope->spawn(fn() => $i));
}

var_dump($scopes[0]->isFinished());

// Innermost first: an outer scope's object, released first, walks its whole subtree (dev/SECURITY.md).
unset($scope);

while ($scopes) {
    array_pop($scopes);
}

?>
--EXPECT--
bool(true)
