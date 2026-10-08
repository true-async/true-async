--TEST--
Context: a value released at the teardown's last step whose destructor calls Scope::disposeAfterTimeout() arms no timer, as async is off
--FILE--
<?php

use Async\Scope;
use function Async\root_context;
use function Async\spawn;

class Disposer
{
    public function __construct(public Scope $scope, public Scope $child_scope) {}

    public function __destruct()
    {
        $this->scope->disposeAfterTimeout(1000);
        echo "disposer destructor\n";
    }
}

class Starter
{
    public function __destruct()
    {
        TrueAsync\Test\print_at_teardown();
        spawn(function () {
            $scope = new Scope();
            root_context()->set('disposer', new Disposer($scope, Scope::inherit($scope)));
        });
    }
}

$starter = new Starter();
echo "end\n";

?>
--EXPECT--
end
teardown: done
disposer destructor
