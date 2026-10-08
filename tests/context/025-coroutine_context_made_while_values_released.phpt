--TEST--
Context: a value of a coroutine's context whose destructor asks for coroutine_context() while the coroutine's values are released gets a new empty context, freed with the coroutine
--FILE--
<?php

use function Async\coroutine_context;
use function Async\spawn;
use function Async\suspend;

class Value
{
    public function __destruct()
    {
        $context = coroutine_context();
        echo "destructor: key ", $context->has('key') ? "held" : "absent", "\n";
        $GLOBALS['weak_context'] = WeakReference::create($context);
    }
}

spawn(function () {
    coroutine_context()->set('key', new Value());
});
while (!isset($weak_context)) {
    suspend();
}

var_dump($weak_context->get());
echo "end\n";

?>
--EXPECT--
destructor: key absent
NULL
end
