--TEST--
Context: an idle child scope the script holds keeps its parent, whose context holds the parent's object, out of the GC; once the child goes, the GC collects the parent
--FILE--
<?php

use function Async\await;
use function Async\current_context;

$server = Async\Scope::inherit();
$server->finally(function () {
    echo "server finally\n";
});
await($server->spawn(function () use ($server) {
    current_context()->set('server', $server);
}));
$request = Async\Scope::inherit($server);
unset($server);

gc_collect_cycles();
echo "after the first run\n";

// The child reaches the parent's object through its context's walk.
$member = $request->spawn(fn() => current_context()->get('server')->spawn(fn() => "spawned in the server scope"));
var_dump(await(await($member)));
unset($member);

// Found live again: only the child scope's disposal can hand the object back to the GC.
gc_collect_cycles();
echo "after the second run\n";

unset($request);
gc_collect_cycles();
echo "end\n";

?>
--EXPECT--
after the first run
string(27) "spawned in the server scope"
after the second run
server finally
end
