--TEST--
The "Unhandled exception in Future" report reads a message that a property hook builds, and frees it
--FILE--
<?php
class HookedMessage extends Exception
{
    protected $message {
        get => str_repeat("x", 3) . "-built";
    }
}

$state = new Async\FutureState();
$state->error(new HookedMessage());
unset($state);
echo "end\n";
?>
--EXPECTF--
Warning: Future was never used; call await(), map(), catch(), finally() or ignore() to suppress this warning. Created at %s:%d in Unknown on line 0

Warning: Unhandled exception in Future: xxx-built; use catch() or ignore() to handle. Created at %s:%d, completed at %s:%d in Unknown on line 0
end
