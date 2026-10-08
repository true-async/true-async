--TEST--
Scope: a member of the innermost of 20 000 nested scopes finishes in time linear in the depth, as each parent walks only its other child scopes for completion
--INI--
max_execution_time=20
memory_limit=1G
--FILE--
<?php

use Async\Scope;
use function Async\await;

$scope = Scope::inherit();
$scopes = [$scope];

for ($i = 0; $i < 20000; $i++) {
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
