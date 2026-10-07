--TEST--
Futures made by shutdown functions have no file to report: their warnings and a second complete() name none
--FILE--
<?php
$state = new Async\FutureState();

register_shutdown_function(['Async\Future', 'completed'], 1);
register_shutdown_function(['Async\Future', 'failed'], new Exception("boom"));
register_shutdown_function([$state, 'complete'], 1);
register_shutdown_function(function () use ($state) {
    try {
        $state->complete(2);
    } catch (Throwable $e) {
        echo $e->getMessage(), "\n";
    }

    $state->ignore();
});

echo "end\n";
?>
--EXPECT--
end

Warning: Future was never used; call await(), map(), catch(), finally() or ignore() to suppress this warning in Unknown on line 0

Warning: Future was never used; call await(), map(), catch(), finally() or ignore() to suppress this warning in Unknown on line 0

Warning: Unhandled exception in Future: boom; use catch() or ignore() to handle in Unknown on line 0
FutureState is already completed at Unknown:0
