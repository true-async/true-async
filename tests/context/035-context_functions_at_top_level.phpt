--TEST--
Context: at the top level current_context() is the root context and coroutine_context() is main's; request_context() is null everywhere
--FILE--
<?php

use function Async\await;
use function Async\coroutine_context;
use function Async\current_context;
use function Async\root_context;

var_dump(current_context() === root_context());
var_dump(coroutine_context() === coroutine_context());
var_dump(coroutine_context() === Async\current_coroutine()->getContext());
var_dump(coroutine_context() !== current_context());
var_dump(Async\request_context());

$scope = new Async\Scope();
var_dump(await($scope->spawn(fn() => Async\request_context())));

?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
NULL
NULL
