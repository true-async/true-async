--TEST--
Context: current_context() refuses while a finished coroutine releases what it held after it left a user scope, and reads the root context after it left the global scope
--FILE--
<?php

use function Async\coroutine_context;
use function Async\current_context;
use function Async\root_context;
use function Async\suspend;

class Probe
{
    public function __destruct()
    {
        try {
            $tenant = current_context()->find('tenant');
            echo "destructor: ", $tenant, "\n";
        } catch (Async\AsyncException $e) {
            echo "destructor: ", $e->getMessage(), "\n";
        }
    }
}

root_context()->set('tenant', 'root');

$isolated = new Async\Scope();
$isolated->spawn(function () {
    current_context()->set('tenant', 'isolated');
    coroutine_context()->set('probe', new Probe());
});
suspend();
suspend();

Async\spawn(function () {
    coroutine_context()->set('probe', new Probe());
});
suspend();
suspend();
echo "end\n";

?>
--EXPECT--
destructor: The current scope is not defined
destructor: root
end
