<?php
// B20: N child scopes leave their parent newest first (scope/125's claim, dev/SECURITY.md S9.8).
// Usage: b20.php <children>. Each child knows its index in the parent's vector, so leaving is
// constant time. One operation is one child scope.
use Async\Scope;

gc_disable();
$children = (int) ($argv[1] ?? 100000);
$parent = new Scope();
$scopes = [];

for ($i = 0; $i < $children; $i++) {
    $scopes[] = Scope::inherit($parent);
}

while ($scopes) {
    array_pop($scopes);
}
