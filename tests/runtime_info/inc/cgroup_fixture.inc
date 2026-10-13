<?php

/* A directory for TrueAsync\Test\cpu_limits(): `$files` maps a path under it ("/proc/self/cgroup",
 * "/proc/self/mountinfo", the cgroup files under the mount points that mountinfo names) to its contents. */
function cgroup_fixture(array $files): string
{
    $root = sys_get_temp_dir() . '/runtime_info_' . getmypid() . '_' . bin2hex(random_bytes(4));

    foreach ($files as $path => $contents) {
        if (!is_dir(dirname($root . $path))) {
            mkdir(dirname($root . $path), 0777, true);
        }

        file_put_contents($root . $path, $contents);
    }

    return $root;
}

function cgroup_fixture_remove(string $root): void
{
    $entries = new RecursiveIteratorIterator(
        new RecursiveDirectoryIterator($root, FilesystemIterator::SKIP_DOTS),
        RecursiveIteratorIterator::CHILD_FIRST
    );

    foreach ($entries as $entry) {
        $entry->isDir() ? rmdir($entry->getPathname()) : unlink($entry->getPathname());
    }

    rmdir($root);
}
