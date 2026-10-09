--TEST--
Async\signal(): Futures of two signals kept in a static variable outlive the request's shutdown; their watches go with it and the Futures are freed after
--FILE--
<?php
use Async\Signal;
use function Async\signal;

function keep(): void
{
    static $futures = [];

    $futures[] = signal(Signal::SIGUSR1);
    $futures[] = signal(Signal::SIGUSR1);
    $futures[] = signal(Signal::SIGUSR2);

    foreach ($futures as $future) {
        $future->ignore();
    }
}

keep();
echo "end\n";
?>
--EXPECT--
end
