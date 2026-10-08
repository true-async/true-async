<?php

/** @generate-class-entries */

namespace Async;

/** Thrown by get() and getLocal() when no level holds the key. */
class ContextException extends AsyncException {}

/**
 * Key-value store of a Scope, shared by every coroutine running in it, or of one coroutine alone.
 * A key is a string or an object; an object key matches that object only.
 *
 * find(), get() and has() read this Context first and then the Contexts of the Scopes above it,
 * skipping a Scope that has none, up to a Scope with no parent: the root Scope or one created by
 * `new Scope()`. A coroutine's Context and one created by `new Context()` have no Scope above. The
 * *Local() forms read this Context alone.
 *
 * @strict-properties
 * @not-serializable
 */
final class Context
{
    /** The value of `$key` at the nearest level that holds it, else null. */
    public function find(string|object $key): mixed {}

    /**
     * The value of `$key` at the nearest level that holds it.
     *
     * @throws ContextException If no level holds the key.
     */
    public function get(string|object $key): mixed {}

    /** Whether a level holds `$key`; a stored null counts as held. */
    public function has(string|object $key): bool {}

    /** The value of `$key` in this Context, else null. */
    public function findLocal(string|object $key): mixed {}

    /**
     * The value of `$key` in this Context.
     *
     * @throws ContextException If this Context does not hold the key.
     */
    public function getLocal(string|object $key): mixed {}

    /** Whether this Context holds `$key`; a stored null counts as held. */
    public function hasLocal(string|object $key): bool {}

    /**
     * Stores `$value` under `$key` in this Context and returns this Context. A key a Scope above
     * holds does not count as held here.
     *
     * @throws AsyncException If this Context holds the key and `$replace` is false.
     */
    public function set(string|object $key, mixed $value, bool $replace = false): Context {}

    /** Removes `$key` from this Context, leaving the Scopes above alone; returns this Context. */
    public function unset(string|object $key): Context {}
}
