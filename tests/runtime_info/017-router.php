<?php

$usage = Async\cpu_usage();

echo 'first call: ', $usage['interval_sec'] === 0.0 && $usage['process_cores'] === 0.0 ? 'zeros' : 'measured';
