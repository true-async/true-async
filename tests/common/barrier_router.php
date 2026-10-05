<?php
// Router of common/http_server_workers.phpt. /hold marks itself started and waits for /release, so
// it answers "released" only when another process of the server serves /release meanwhile.
$holding = $_SERVER['DOCUMENT_ROOT'] . '/holding';
$released = $_SERVER['DOCUMENT_ROOT'] . '/released';

if ($_SERVER['REQUEST_URI'] === '/release') {
    touch($released);
    echo 'ok';
    return;
}

touch($holding);

for ($i = 0; $i < 100 && !file_exists($released); $i++) {
    usleep(100000);
    clearstatcache();
}

echo file_exists($released) ? 'released' : 'timeout';
