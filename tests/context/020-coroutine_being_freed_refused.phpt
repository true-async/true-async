--TEST--
Context: current_coroutine(), coroutine_context() and Coroutine::getContext() refuse a finished coroutine whose object is being freed
--FILE--
<?php

use function Async\current_coroutine;
use function Async\spawn;
use function Async\suspend;

class Probe
{
    public function __destruct()
    {
        $calls = [
            'current_coroutine' => fn() => Async\current_coroutine(),
            'coroutine_context' => fn() => Async\coroutine_context(),
            // The WeakReference, registered after the WeakMap entry, still reaches the object.
            'getContext' => fn() => $GLOBALS['weak_coroutine']->get()->getContext(),
        ];

        foreach ($calls as $name => $call) {
            try {
                $call();
                echo "$name: no throw\n";
            } catch (Async\AsyncException $e) {
                echo "$name: ", $e->getMessage(), "\n";
            }
        }
    }
}

// A WeakMap value is released by the coroutine object's free_obj, while the coroutine is still current.
$map = new WeakMap();
spawn(function () use ($map) {
    $map[current_coroutine()] = new Probe();
    $GLOBALS['weak_coroutine'] = WeakReference::create(current_coroutine());
});
suspend();
echo "end\n";

?>
--EXPECT--
current_coroutine: The current coroutine is not defined
coroutine_context: The current coroutine is not defined
getContext: The coroutine is being freed
end
