--TEST--
Context: a shutdown function and a destructor at shutdown read the root context the script filled; main's coroutine context is not theirs
--FILE--
<?php

use function Async\coroutine_context;
use function Async\current_context;
use function Async\root_context;

function show(string $where): void
{
    echo $where, ": main's ", var_export(coroutine_context()->find('main'), true),
        ", current ", var_export(current_context()->find('root'), true),
        ", root ", var_export(root_context()->find('root'), true), "\n";
}

class AtShutdown
{
    public function __destruct()
    {
        show('destructor');
    }
}

coroutine_context()->set('main', 'M');
root_context()->set('root', 'R');
register_shutdown_function(fn() => show('shutdown function'));
$keep = new AtShutdown();
echo "end\n";

?>
--EXPECT--
end
shutdown function: main's NULL, current 'R', root 'R'
destructor: main's NULL, current 'R', root 'R'
