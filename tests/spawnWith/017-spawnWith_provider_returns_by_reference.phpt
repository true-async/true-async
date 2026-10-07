--TEST--
spawn_with(): a provideScope() declared to return by reference names its Scope, or the current scope for null
--FILE--
<?php
use Async\Scope;
use Async\ScopeProvider;
use function Async\spawn_with;
use function Async\await;

class P implements ScopeProvider {
    public ?Scope $scope;
    function __construct(?Scope $s) { $this->scope = $s; }
    function &provideScope(): ?Scope { return $this->scope; }
}
$s = new Scope();
$c = spawn_with(new P($s), function () { return "in scope"; });
echo await($c), "\n";
$c = spawn_with(new P(null), function () { return "in current"; });
echo await($c), "\n";
?>
--EXPECT--
in scope
in current
