--TEST--
Context: the GC destroys the object of a scope whose context holds it and whose finally handler has not run; the handler runs and the scope goes after it
--FILE--
<?php

use function Async\await;
use function Async\current_context;
use function Async\suspend;

$scope = Async\Scope::inherit();
$scope->finally(function () {
    echo "finally ran\n";
});
$context = await($scope->spawn(fn() => current_context()));
$context->set('scope', $scope);
$weak_context = WeakReference::create($context);
unset($scope, $context);

gc_collect_cycles();
echo "after the first run\n";
suspend();
suspend();
gc_collect_cycles();
var_dump($weak_context->get());
echo "end\n";

?>
--EXPECT--
finally ran
after the first run
NULL
end
