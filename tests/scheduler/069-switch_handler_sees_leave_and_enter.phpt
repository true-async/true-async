--TEST--
A switch handler that stays registered sees its coroutine leave at each suspend() and enter again when it resumes
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$coroutine = spawn(function () {
    TrueAsync\Test\add_printing_switch_handler(Async\current_coroutine());
    echo "suspends\n";
    suspend();
    echo "resumed\n";
});

await($coroutine);
echo "main end\n";
?>
--EXPECTF--
suspends
leave #%d
enter #%d
resumed
main end
