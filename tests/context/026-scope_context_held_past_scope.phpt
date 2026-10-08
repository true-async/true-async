--TEST--
Context: a scope's context held after its scope is freed answers from its own table and no longer reads the parents
--FILE--
<?php

use function Async\await;
use function Async\current_context;
use function Async\root_context;

root_context()->set('root', 'R');

$scope = Async\Scope::inherit();
$context = await($scope->spawn(fn() => current_context()));
$context->set('own', 'O');
var_dump($context->find('root'));

unset($scope);
var_dump($context->find('root'));
var_dump($context->find('own'));

$detached = new Async\Context();
var_dump($detached->find('root'));

?>
--EXPECT--
string(1) "R"
NULL
string(1) "O"
NULL
