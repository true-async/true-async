--TEST--
Context: in a Fiber current_context() is the root context, also when the Fiber starts in a coroutine of a scope
--FILE--
<?php

use function Async\await;
use function Async\current_context;
use function Async\root_context;

root_context()->set('root', 'R');

$fiber = new Fiber(function () {
    var_dump(current_context() === root_context());
});
$fiber->start();

$scope = Async\Scope::inherit();
await($scope->spawn(function () {
    current_context()->set('scope', 'S');
    $fiber = new Fiber(function () {
        var_dump(current_context()->find('scope'));
        var_dump(current_context()->find('root'));
    });
    $fiber->start();
}));

?>
--EXPECT--
bool(true)
NULL
string(1) "R"
