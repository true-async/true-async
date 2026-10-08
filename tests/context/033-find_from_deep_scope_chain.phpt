--TEST--
Context: find() from the bottom of 10 000 nested scopes reaches a middle scope's context and the root context
--FILE--
<?php

use function Async\await;
use function Async\current_context;
use function Async\root_context;

root_context()->set('root', 'R');

$scopes = [];
$scope = null;

for ($i = 0; $i < 10000; $i++) {
    $scope = Async\Scope::inherit($scope);
    $scopes[] = $scope;
}

await($scopes[5000]->spawn(fn() => current_context()->set('middle', 'M')));
var_dump(await($scope->spawn(fn() => [current_context()->find('root'), current_context()->find('middle')])));
var_dump(await($scope->spawn(fn() => current_context()->has('missing'))));

?>
--EXPECT--
array(2) {
  [0]=>
  string(1) "R"
  [1]=>
  string(1) "M"
}
bool(false)
