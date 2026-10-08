--TEST--
Context: a Future's map() callback runs in the chain drain of the global scope, so its current_context() is the root context, not the subscriber scope's
--FILE--
<?php

use function Async\await;
use function Async\current_context;
use function Async\root_context;

$scope = new Async\Scope();
var_dump(await($scope->spawn(function () {
    current_context()->set('request_id', 7);

    return await(Async\Future::completed(1)->map(
        fn() => [current_context() === root_context(), current_context()->find('request_id')]
    ));
})));

?>
--EXPECT--
array(2) {
  [0]=>
  bool(true)
  [1]=>
  NULL
}
