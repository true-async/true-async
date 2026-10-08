--TEST--
Context: a value of a freed scope's context whose destructor disposes the parent scope runs after the free, and the parent goes cleanly
--FILE--
<?php

use function Async\await;
use function Async\current_context;

class DisposesParent
{
    public function __construct(private Async\Scope $parent)
    {
    }

    public function __destruct()
    {
        echo "destructor: disposing the parent\n";
        $this->parent->dispose();
        echo "parent closed: ", var_export($this->parent->isClosed(), true), "\n";
    }
}

$parent = new Async\Scope();
$child = Async\Scope::inherit($parent);
await($child->spawn(fn() => current_context()->set('value', new DisposesParent($parent))));
unset($parent);

$child->dispose();
unset($child);
echo "end\n";

?>
--EXPECT--
destructor: disposing the parent
parent closed: true
end
