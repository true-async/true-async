/*
   +----------------------------------------------------------------------+
   | Copyright © TrueAsync contributors.                                  |
   +----------------------------------------------------------------------+
   | This source file is subject to the Modified BSD License that is      |
   | bundled with this package in the file LICENSE.                       |
   |                                                                      |
   | SPDX-License-Identifier: BSD-3-Clause                                |
   +----------------------------------------------------------------------+
   | Authors: Edmond <edmondifthen@proton.me>                             |
   +----------------------------------------------------------------------+
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "Zend/zend_closures.h"
#include "Zend/zend_hrtime.h"
#include "php_true_async.h"
#include "collector.h"
#include "scheduler.h"
#include "exceptions.h"
#include "future.h"
#include "os_signal.h"
#include "scope.h"

/* The walk (dev/plans/S7.md, section 3.3) keeps its colours and counts in its own tables, so it never
 * touches a reference count or the GC_INFO of PHP's collector:
 *
 * 1. Candidates: the parked coroutines every record of whose wait names its target (3.1).
 * 2. Wake edges: each record gives an edge from its target to its waiter. A target that is a running
 *    coroutine, one waiting for an outside source, or an event an outside source will complete (a
 *    signal() Future, seeded first), makes its waiters live, and theirs, before anything else is
 *    walked.
 *    A scope a candidate is in gives a reach node per scope up to the root, with an edge to the
 *    candidate or the child scope's node (10).
 * 3. Count: from the candidates left, every node reachable through what the nodes own is counted:
 *    how many references the walked nodes hold to it, the registry's to each candidate included.
 *    Then a reach node whose Scope object the count never met is held from outside and live; one it
 *    met gets an edge from the object's node.
 * 4. Spread: a node with more references than counted is held from outside and live; what a live
 *    node owns is live, and a live node makes live the nodes its edges lead to.
 * 5. The candidates left are the result. */

#define COLLECTOR_NONE UINT32_MAX

/* A candidate coroutine: its references are its object's, its parked stack's and its records'. */
#define COLLECTOR_NODE_CANDIDATE (1u << 0)
/* Live before the count: a coroutine that is not a candidate, a weakly referenced object, a candidate
 * a wake edge made live. Its references are never counted, so whatever it alone holds keeps a
 * reference from outside the count and is live by it. */
#define COLLECTOR_NODE_KNOWN_LIVE (1u << 1)
#define COLLECTOR_NODE_LIVE (1u << 2)
/* An event of the extension: its holders are counted in `ref_count`. */
#define COLLECTOR_NODE_EVENT (1u << 3)
/* A reach node (S7.md 10): no value, so nothing counts or walks it; live once a node with an edge to it
 * is, or once its holder is held from outside. */
#define COLLECTOR_NODE_REACH (1u << 4)

/* An object, an array, a reference or an event the walk met. */
typedef struct
{
	void *address; /* a zend_refcounted, an async_event_t with COLLECTOR_NODE_EVENT, a key with COLLECTOR_NODE_REACH */
	async_collector_event_references_t event_references; /* an event's reporter; NULL: it reports none */
	uint32_t internal;   /* references the walked nodes hold to it, the registry's to a candidate included */
	uint32_t first_edge; /* the first edge from it: to a candidate waiting for it, or a reach; COLLECTOR_NONE */
	uint8_t flags;
} collector_node_t;

typedef struct
{
	uint32_t to;   /* the node the edge's source makes live: a candidate it wakes, or a reach */
	uint32_t next; /* the source's next edge; COLLECTOR_NONE */
} collector_edge_t;

/* An object whose being held from outside makes a reach node live. */
typedef struct
{
	zend_object *object;
	uint32_t node;
} collector_holder_t;

/* What a reported reference does: the same reporters run in every pass, as PHP's collector walks the
 * same references to mark and to scan (zend_gc.c, gc_mark_grey and gc_scan_black). */
typedef enum
{
	COLLECTOR_PASS_WAKE_EDGES, /* only collector_target reports, each one an edge */
	COLLECTOR_PASS_COUNT,      /* a reference counts toward its node */
	COLLECTOR_PASS_SPREAD,     /* a reference from a live node makes its node live */
} collector_pass_t;

struct _async_collector_s
{
	HashTable index; /* collector_key() of a node's address to its position in `nodes` */
	collector_node_t *nodes;
	uint32_t node_count;
	uint32_t node_capacity;
	collector_edge_t *edges;
	uint32_t edge_count;
	uint32_t edge_capacity;
	uint32_t *worklist; /* positions of nodes made live and not yet walked */
	uint32_t worklist_count;
	uint32_t worklist_capacity;
	collector_holder_t *holders; /* read after the count, when it shows who is held from outside */
	uint32_t holder_count;
	uint32_t holder_capacity;
	/* The symbol tables reported in this pass: an include or an eval in a function hands the function's
	 * table to its frame (zend_vm_def.h, ZEND_INCLUDE_OR_EVAL), so two frames return the same one. */
	HashTable symbol_tables;
	zend_get_gc_buffer frame_buffer; /* the frames' references; get_gc answers in the engine's own */
	collector_pass_t pass;
	uint32_t waiter; /* the candidate whose records report, in the wake-edge pass */
	size_t ceiling;  /* the memory the tables may grow to; 0: none */
	bool failed;     /* the passes disagreed, or the tables would pass the ceiling: the run finds nothing */
};

/* Nodes are allocated with at least 8-byte alignment: the low bits would leave most buckets of the
 * index empty. */
static zend_always_inline zend_ulong collector_key(const void *address)
{
	return (zend_ulong) ((uintptr_t) address >> 3);
}

/* Positions are uint32_t with COLLECTOR_NONE at the top, so no table outgrows half of that range. */
static void collector_grow(void **array, uint32_t *capacity, const size_t element_size)
{
	if (UNEXPECTED(*capacity > UINT32_MAX / 2)) {
		zend_error_noreturn(E_ERROR, "The coroutine collector cannot track more than %u entries", *capacity);
	}

	*capacity = *capacity == 0 ? 16 : *capacity * 2;
	*array = safe_erealloc(*array, *capacity, element_size, 0);
}

static uint32_t collector_node_add(async_collector_t *collector, void *address, const uint8_t flags)
{
	if (UNEXPECTED(collector->node_count == collector->node_capacity)) {
		collector_grow((void **) &collector->nodes, &collector->node_capacity, sizeof(collector_node_t));
	}

	collector_node_t *node = &collector->nodes[collector->node_count];
	node->address = address;
	node->event_references = NULL;
	node->internal = 0;
	node->first_edge = COLLECTOR_NONE;
	node->flags = flags;

	return collector->node_count++;
}

/* Whether `bytes` more keep the memory in use under the ceiling, with a chunk to spare for the small
 * allocations of the walk (the heap wants one free before it maps another). A run that would pass it
 * stops and finds nothing: the automatic run's ceiling is memory_limit, and a fatal error there would
 * end the request. */
static bool collector_has_room(async_collector_t *collector, const size_t bytes)
{
	if (EXPECTED(collector->ceiling == 0 ||
				 zend_memory_usage(true) + bytes + ZEND_MM_CHUNK_SIZE <= collector->ceiling)) {
		return true;
	}

	collector->failed = true;

	return false;
}

/* Whether the next doubling of `nodes` and of the index, which grow in step, and a worklist entry per
 * node fit under the ceiling. */
static bool collector_has_node_room(async_collector_t *collector)
{
	if (EXPECTED(collector->node_count != collector->node_capacity)) {
		return true;
	}

	return collector_has_room(collector,
							  (size_t) collector->node_capacity * 2 *
									  (sizeof(collector_node_t) + sizeof(Bucket) + 3 * sizeof(uint32_t)));
}

/* The node of `ref`, added on first sight; COLLECTOR_NONE once the run failed. Every candidate is
 * added before anything else, so a coroutine met here is not one: it runs or waits for an outside
 * source. */
static uint32_t collector_node_of(async_collector_t *collector, zend_refcounted *ref)
{
	if (UNEXPECTED(collector->failed || !collector_has_node_room(collector))) {
		return COLLECTOR_NONE;
	}

	zval *position = zend_hash_index_lookup(&collector->index, collector_key(ref));

	if (EXPECTED(Z_TYPE_P(position) == IS_LONG)) {
		return (uint32_t) Z_LVAL_P(position);
	}

	uint8_t flags = 0;

	if (UNEXPECTED(GC_TYPE(ref) == IS_OBJECT &&
				   (((zend_object *) ref)->ce == async_ce_coroutine || (GC_FLAGS(ref) & IS_OBJ_WEAKLY_REFERENCED)))) {
		flags = COLLECTOR_NODE_KNOWN_LIVE;
	}

	const uint32_t node = collector_node_add(collector, ref, flags);
	ZVAL_LONG(position, node);

	return node;
}

static uint32_t collector_event_node_of(async_collector_t *collector,
										async_event_t *event,
										async_collector_event_references_t references)
{
	if (UNEXPECTED(collector->failed || !collector_has_node_room(collector))) {
		return COLLECTOR_NONE;
	}

	zval *position = zend_hash_index_lookup(&collector->index, collector_key(event));

	if (EXPECTED(Z_TYPE_P(position) == IS_LONG)) {
		return (uint32_t) Z_LVAL_P(position);
	}

	/* An event embedded in its object counts no holders: the object is its node. */
	ZEND_ASSERT(!(event->flags & ASYNC_EVENT_F_ZEND_OBJ));

	const uint32_t node = collector_node_add(collector, event, COLLECTOR_NODE_EVENT);
	collector->nodes[node].event_references = references;
	ZVAL_LONG(position, node);

	return node;
}

static zend_always_inline uint32_t collector_node_refcount(const collector_node_t *node)
{
	if (UNEXPECTED(node->flags & COLLECTOR_NODE_EVENT)) {
		return ((const async_event_t *) node->address)->ref_count;
	}

	return GC_REFCOUNT((zend_refcounted *) node->address);
}

static void collector_worklist_push(async_collector_t *collector, const uint32_t node)
{
	if (UNEXPECTED(collector->worklist_count == collector->worklist_capacity)) {
		collector_grow((void **) &collector->worklist, &collector->worklist_capacity, sizeof(uint32_t));
	}

	collector->worklist[collector->worklist_count++] = node;
}

static void collector_mark_live(async_collector_t *collector, const uint32_t node)
{
	if (EXPECTED(collector->nodes[node].flags & COLLECTOR_NODE_LIVE)) {
		return;
	}

	collector->nodes[node].flags |= COLLECTOR_NODE_LIVE;
	collector_worklist_push(collector, node);
}

/* The spread's half of a reported reference: the node the count added is live. */
static void collector_spread_to(async_collector_t *collector, const void *address)
{
	const zval *position = zend_hash_index_find(&collector->index, collector_key(address));

	if (UNEXPECTED(position == NULL)) {
		ZEND_ASSERT(0 && "the spread meets only what the count added");
		collector->failed = true;
		return;
	}

	collector_mark_live(collector, (uint32_t) Z_LVAL_P(position));
}

static void collector_reference_counted(async_collector_t *collector, zend_refcounted *ref)
{
	if (UNEXPECTED(GC_FLAGS(ref) & GC_IMMUTABLE)) {
		return;
	}

	if (EXPECTED(collector->pass == COLLECTOR_PASS_COUNT)) {
		/* Apart: the lookup may move `nodes`. */
		const uint32_t node = collector_node_of(collector, ref);

		if (EXPECTED(node != COLLECTOR_NONE)) {
			collector->nodes[node].internal++;
		}

		return;
	}

	collector_spread_to(collector, ref);
}

static void collector_event_reference(async_collector_t *collector,
									  async_event_t *event,
									  async_collector_event_references_t references)
{
	if (EXPECTED(collector->pass == COLLECTOR_PASS_COUNT)) {
		const uint32_t node = collector_event_node_of(collector, event, references);

		if (EXPECTED(node != COLLECTOR_NONE)) {
			collector->nodes[node].internal++;
		}

		return;
	}

	collector_spread_to(collector, event);
}

static void collector_reference(async_collector_t *collector, zval *value)
{
	if (UNEXPECTED(Z_TYPE_P(value) == IS_INDIRECT)) {
		value = Z_INDIRECT_P(value);
	}

	if (EXPECTED(Z_COLLECTABLE_P(value))) {
		collector_reference_counted(collector, Z_COUNTED_P(value));
	}
}

static void collector_table_references(async_collector_t *collector, HashTable *table)
{
	zval *value;

	ZEND_HASH_FOREACH_VAL(table, value)
	{
		collector_reference(collector, value);
	}
	ZEND_HASH_FOREACH_END();
}

void async_collector_report_object(async_collector_t *collector, zend_object *object)
{
	collector_reference_counted(collector, (zend_refcounted *) object);
}

void async_collector_report_zval(async_collector_t *collector, zval *value)
{
	collector_reference(collector, value);
}

void async_collector_report_event(async_collector_t *collector,
								  async_event_t *event,
								  async_collector_event_references_t references)
{
	collector_event_reference(collector, event, references);
}

/* An edge from `node` to `to`: a live `node` makes `to` live. */
static void collector_edge_add(async_collector_t *collector, const uint32_t node, const uint32_t to)
{
	if (UNEXPECTED(node == COLLECTOR_NONE || to == COLLECTOR_NONE)) {
		return;
	}

	/* An edge per record, not per node: many candidates awaiting the same items make far more edges than nodes. */
	if (UNEXPECTED(collector->edge_count == collector->edge_capacity)) {
		if (UNEXPECTED(
					!collector_has_room(collector, (size_t) collector->edge_capacity * 2 * sizeof(collector_edge_t)))) {
			return;
		}

		collector_grow((void **) &collector->edges, &collector->edge_capacity, sizeof(collector_edge_t));
	}

	collector_edge_t *edge = &collector->edges[collector->edge_count];
	edge->to = to;
	edge->next = collector->nodes[node].first_edge;
	collector->nodes[node].first_edge = collector->edge_count++;
}

/* In the wake-edge pass: an edge from the target's node to the candidate whose records report. */
static void collector_wake_edge_add(async_collector_t *collector, const uint32_t node)
{
	collector_edge_add(collector, node, collector->waiter);
}

uint32_t async_collector_reach_node(async_collector_t *collector, const void *key, bool *added)
{
	*added = false;

	if (UNEXPECTED(collector->failed || !collector_has_node_room(collector))) {
		return COLLECTOR_NONE;
	}

	zval *position = zend_hash_index_lookup(&collector->index, collector_key(key));

	if (EXPECTED(Z_TYPE_P(position) == IS_LONG)) {
		return (uint32_t) Z_LVAL_P(position);
	}

	/* Only a key: nothing reads through it. */
	const uint32_t node = collector_node_add(collector, (void *) key, COLLECTOR_NODE_REACH);
	ZVAL_LONG(position, node);
	*added = true;

	return node;
}

void async_collector_report_reach(async_collector_t *collector, const uint32_t from, const uint32_t to)
{
	collector_edge_add(collector, from, to);
}

void async_collector_report_holder(async_collector_t *collector, zend_object *holder, const uint32_t node)
{
	if (UNEXPECTED(node == COLLECTOR_NONE)) {
		return;
	}

	if (UNEXPECTED(collector->holder_count == collector->holder_capacity)) {
		if (UNEXPECTED(!collector_has_room(collector,
										   (size_t) collector->holder_capacity * 2 * sizeof(collector_holder_t)))) {
			return;
		}

		collector_grow((void **) &collector->holders, &collector->holder_capacity, sizeof(collector_holder_t));
	}

	collector->holders[collector->holder_count].object = holder;
	collector->holders[collector->holder_count].node = node;
	collector->holder_count++;
}

void async_collector_report_live_reach(async_collector_t *collector, const uint32_t node)
{
	if (EXPECTED(node != COLLECTOR_NONE)) {
		collector->nodes[node].flags |= COLLECTOR_NODE_KNOWN_LIVE;
	}
}

void async_collector_report_target(async_collector_t *collector, zend_object *target, const bool owned)
{
	if (EXPECTED(collector->pass != COLLECTOR_PASS_WAKE_EDGES)) {
		if (EXPECTED(owned)) {
			collector_reference_counted(collector, (zend_refcounted *) target);
		}

		return;
	}

	collector_wake_edge_add(collector, collector_node_of(collector, (zend_refcounted *) target));
}

uint32_t async_collector_report_reach_target(async_collector_t *collector, const void *key, bool *added)
{
	if (EXPECTED(collector->pass != COLLECTOR_PASS_WAKE_EDGES)) {
		*added = false;
		return COLLECTOR_NONE;
	}

	const uint32_t node = async_collector_reach_node(collector, key, added);

	collector_wake_edge_add(collector, node);

	return node;
}

void async_collector_report_reach_source(async_collector_t *collector, zend_object *source, const uint32_t to)
{
	if (UNEXPECTED(to == COLLECTOR_NONE)) {
		return;
	}

	collector_edge_add(collector, collector_node_of(collector, (zend_refcounted *) source), to);
}

void async_collector_report_live_event(async_collector_t *collector,
									   async_event_t *event,
									   async_collector_event_references_t references)
{
	const uint32_t node = collector_event_node_of(collector, event, references);

	if (EXPECTED(node != COLLECTOR_NONE)) {
		collector->nodes[node].flags |= COLLECTOR_NODE_KNOWN_LIVE;
	}
}

void async_collector_report_outside(async_collector_t *collector)
{
	if (EXPECTED(collector->pass != COLLECTOR_PASS_WAKE_EDGES)) {
		return;
	}

	collector->nodes[collector->waiter].flags |= COLLECTOR_NODE_KNOWN_LIVE;
}

void async_collector_report_event_target(async_collector_t *collector,
										 async_event_t *target,
										 async_collector_event_references_t references,
										 const bool owned)
{
	if (EXPECTED(collector->pass != COLLECTOR_PASS_WAKE_EDGES)) {
		if (EXPECTED(owned)) {
			collector_event_reference(collector, target, references);
		}

		return;
	}

	collector_wake_edge_add(collector, collector_event_node_of(collector, target, references));
}

static void collector_record_target(const async_coroutine_event_callback_t *record, void *collector)
{
	record->event_callback.kind->collector_target(record, collector);
}

/* An internal function's frame, read here and not by zend_unfinished_execution_gc_ex, which asserts
 * that an internal frame has no extra named parameters (zend_execute.c) while a variadic internal
 * function gets them. The call owns all of it until it returns: await(spawn(...)) holds the
 * coroutine in its argument. */
static void collector_internal_frame_references(async_collector_t *collector, zend_execute_data *frame)
{
	const uint32_t call_info = ZEND_CALL_INFO(frame);

	if (UNEXPECTED(call_info & ZEND_CALL_RELEASE_THIS)) {
		collector_reference_counted(collector, (zend_refcounted *) Z_OBJ(frame->This));
	}

	if (UNEXPECTED(call_info & ZEND_CALL_CLOSURE)) {
		collector_reference_counted(collector, (zend_refcounted *) ZEND_CLOSURE_OBJECT(frame->func));
	}

	zval *arguments = ZEND_CALL_ARG(frame, 1);
	const uint32_t argument_count = ZEND_CALL_NUM_ARGS(frame);

	for (uint32_t i = 0; i < argument_count; i++) {
		collector_reference(collector, &arguments[i]);
	}

	if (UNEXPECTED(call_info & ZEND_CALL_HAS_EXTRA_NAMED_PARAMS)) {
		collector_reference_counted(collector, (zend_refcounted *) frame->extra_named_params);
	}
}

/* The parked stack, frame by frame to the coroutine's root frame (S7.md 3.2). A frame left out only
 * hides a finding: what it holds then counts as held from outside. */
static void collector_stack_references(async_collector_t *collector, const async_coroutine_t *coroutine)
{
	/* With zend_execute_ex replaced (a profiler), the VM marks its own user calls ZEND_CALL_TOP too, and
	 * one on $this, parent:: or self:: takes no reference: a user frame's pin is then left uncounted,
	 * which can only hide a finding. The VM never marks an internal frame so. */
	const bool own_vm = zend_execute_ex == execute_ex;

	for (zend_execute_data *frame = coroutine->fiber_context->execute_data; frame != NULL;
		 frame = frame->prev_execute_data) {
		/* A generator's frame: zend_generator_frame_gc is not exported. One with no function is a
		 * generator's fake frame. */
		if (UNEXPECTED(frame->func == NULL || (ZEND_CALL_INFO(frame) & ZEND_CALL_GENERATOR))) {
			continue;
		}

		/* zend_call_function() keeps the object of the frame it pushes until the call returns
		 * (pinned_this), and no flag of the frame says so. */
		if ((own_vm || !ZEND_USER_CODE(frame->func->type)) &&
			UNEXPECTED((ZEND_CALL_INFO(frame) &
						(ZEND_CALL_TOP | ZEND_CALL_CODE | ZEND_CALL_HAS_THIS | ZEND_CALL_RELEASE_THIS)) ==
					   (ZEND_CALL_TOP | ZEND_CALL_HAS_THIS))) {
			collector_reference_counted(collector, (zend_refcounted *) Z_OBJ(frame->This));
		}

		if (UNEXPECTED(!ZEND_USER_CODE(frame->func->type))) {
			collector_internal_frame_references(collector, frame);
			continue;
		}

		/* A frame unwinding an exception finds its position through EG(opline_before_exception),
		 * which belongs to the running context, not to this parked one. */
		if (UNEXPECTED(frame->opline->opcode == ZEND_HANDLE_EXCEPTION)) {
			continue;
		}

		collector->frame_buffer.cur = collector->frame_buffer.start;

		HashTable *symbol_table = zend_unfinished_execution_gc_ex(frame, frame->call, &collector->frame_buffer, false);

		for (zval *value = collector->frame_buffer.start; value < collector->frame_buffer.cur; value++) {
			collector_reference(collector, value);
		}

		/* The global scope is a root: what a global variable holds stays live. */
		if (UNEXPECTED(symbol_table != NULL && symbol_table != &EG(symbol_table))) {
			const zend_ulong key = collector_key(symbol_table);

			if (EXPECTED(zend_hash_index_add_empty_element(&collector->symbol_tables, key) != NULL)) {
				collector_table_references(collector, symbol_table);
			}
		}
	}
}

/* What PHP's collector reads from an object (gc_mark_grey): the table and the hash get_gc returns. A
 * candidate adds its stack and the references its records own. */
static void collector_object_references(async_collector_t *collector, zend_object *object, const bool is_candidate)
{
	if (UNEXPECTED(OBJ_FLAGS(object) & IS_OBJ_FREE_CALLED)) {
		return;
	}

	/* Their get_gc folds the event into its only holder, and reports nothing while a waiter holds it
	 * too; the walk counts the event as a node of its own. */
	if (UNEXPECTED(object->ce == async_ce_future_state || object->ce == async_ce_future)) {
		async_future_collector_references(object, collector);
		return;
	}

	/* Its get_gc reports the exception handlers, which belong to the internal scope: the route of an
	 * unhandled error calls them, the object gone or not, so what they capture is held from outside. */
	if (UNEXPECTED(object->ce == async_ce_scope)) {
		return;
	}

	zval *table = NULL;
	int count = 0;
	HashTable *properties = object->handlers->get_gc(object, &table, &count);

	/* The table lives in the engine's one get_gc buffer: read through before the next get_gc. */
	for (int i = 0; i < count; i++) {
		collector_reference(collector, &table[i]);
	}

	if (EXPECTED(properties != NULL)) {
		collector_reference_counted(collector, (zend_refcounted *) properties);
	}

	if (UNEXPECTED(is_candidate)) {
		async_coroutine_t *coroutine = async_coroutine_from_object(object);

		collector_stack_references(collector, coroutine);
		async_wait_walk(coroutine, collector_record_target, collector);
	}
}

static void collector_node_references(async_collector_t *collector, const uint32_t node)
{
	const collector_node_t *entry = &collector->nodes[node];

	if (UNEXPECTED(entry->flags & COLLECTOR_NODE_REACH)) {
		return;
	}

	if (UNEXPECTED(entry->flags & COLLECTOR_NODE_EVENT)) {
		if (EXPECTED(entry->event_references != NULL)) {
			entry->event_references(entry->address, collector);
		}

		return;
	}

	zend_refcounted *ref = entry->address;

	switch (GC_TYPE(ref)) {
		case IS_OBJECT:
			collector_object_references(collector, (zend_object *) ref, (entry->flags & COLLECTOR_NODE_CANDIDATE) != 0);
			break;
		case IS_ARRAY:
			collector_table_references(collector, (HashTable *) ref);
			break;
		case IS_REFERENCE:
			collector_reference(collector, &((zend_reference *) ref)->val);
			break;
		default:
			ZEND_UNREACHABLE();
	}
}

/* Counts the records of the coroutine's wait and whether each kind names its target. */
typedef struct
{
	uint32_t records;
	bool all_named;
} collector_wait_shape_t;

static void collector_wait_shape_add(const async_coroutine_event_callback_t *record, void *arg)
{
	collector_wait_shape_t *shape = arg;

	shape->records++;

	if (UNEXPECTED(record->event_callback.kind->collector_target == NULL)) {
		shape->all_named = false;
	}
}

/* S7.md 3.1: parked on a wait of at least one record, all of kinds that name their target. A Fiber's
 * coroutine is held through a pointer the walk cannot count, and its stack continues into its
 * caller's. */
static bool collector_is_candidate(async_coroutine_t *coroutine)
{
	const zend_coroutine_t *zend_coroutine = &coroutine->coroutine;

	if (UNEXPECTED(!ZEND_COROUTINE_IS_SUSPENDED(zend_coroutine) || ZEND_COROUTINE_IS_FIBER(zend_coroutine))) {
		return false;
	}

	collector_wait_shape_t shape = { .records = 0, .all_named = true };
	async_wait_walk(coroutine, collector_wait_shape_add, &shape);

	return shape.records != 0 && shape.all_named;
}

static void collector_init(async_collector_t *collector)
{
	memset(collector, 0, sizeof(*collector));
	zend_hash_init(&collector->index, 16, NULL, NULL, false);
	zend_hash_init(&collector->symbol_tables, 8, NULL, NULL, false);
}

static void collector_destroy(async_collector_t *collector)
{
	zend_hash_destroy(&collector->index);
	zend_hash_destroy(&collector->symbol_tables);

	if (EXPECTED(collector->nodes != NULL)) {
		efree(collector->nodes);
	}

	if (EXPECTED(collector->edges != NULL)) {
		efree(collector->edges);
	}

	if (EXPECTED(collector->worklist != NULL)) {
		efree(collector->worklist);
	}

	if (UNEXPECTED(collector->holders != NULL)) {
		efree(collector->holders);
	}

	if (EXPECTED(collector->frame_buffer.start != NULL)) {
		efree(collector->frame_buffer.start);
	}
}

/* Step 2: the wake edges, and the candidates they make live before the count. */
static void collector_wake_edges(async_collector_t *collector, const uint32_t candidate_count)
{
	collector->pass = COLLECTOR_PASS_WAKE_EDGES;

	for (uint32_t candidate = 0; candidate < candidate_count && EXPECTED(!collector->failed); candidate++) {
		collector->waiter = candidate;
		async_wait_walk(async_coroutine_from_object((zend_object *) collector->nodes[candidate].address),
						collector_record_target,
						collector);
	}

	for (uint32_t node = 0; node < collector->node_count; node++) {
		if (UNEXPECTED(collector->nodes[node].flags & COLLECTOR_NODE_KNOWN_LIVE)) {
			collector_worklist_push(collector, node);
		}
	}

	while (collector->worklist_count != 0) {
		const uint32_t node = collector->worklist[--collector->worklist_count];

		for (uint32_t edge = collector->nodes[node].first_edge; edge != COLLECTOR_NONE;
			 edge = collector->edges[edge].next) {
			collector_node_t *to = &collector->nodes[collector->edges[edge].to];

			if (EXPECTED(!(to->flags & COLLECTOR_NODE_KNOWN_LIVE))) {
				to->flags |= COLLECTOR_NODE_KNOWN_LIVE;
				collector_worklist_push(collector, collector->edges[edge].to);
			}
		}
	}
}

/* Step 3: the nodes array is the queue, so every node is walked once, the ones it adds included. */
static void collector_count(async_collector_t *collector)
{
	collector->pass = COLLECTOR_PASS_COUNT;

	for (uint32_t node = 0; node < collector->node_count && EXPECTED(!collector->failed); node++) {
		if (EXPECTED(!(collector->nodes[node].flags & COLLECTOR_NODE_KNOWN_LIVE))) {
			collector_node_references(collector, node);
		}
	}
}

/* After the count: a holder the count never met is held from outside the walk, so its reach node is
 * live; one it met gets an edge to it, and the spread decides. */
static void collector_holders(async_collector_t *collector)
{
	for (uint32_t i = 0; i < collector->holder_count; i++) {
		const collector_holder_t *holder = &collector->holders[i];
		const zval *position = zend_hash_index_find(&collector->index, collector_key(holder->object));

		if (EXPECTED(position == NULL)) {
			collector->nodes[holder->node].flags |= COLLECTOR_NODE_KNOWN_LIVE;
		} else {
			collector_edge_add(collector, (uint32_t) Z_LVAL_P(position), holder->node);
		}
	}
}

/* Step 4, false when the passes disagree: a node has more references counted than it has (a reporter gave one
 * its holder does not own), or the spread met a node the count did not (a get_gc answered otherwise).
 * Any finding of the run may then be false. */
static bool collector_spread(async_collector_t *collector)
{
	collector->pass = COLLECTOR_PASS_SPREAD;
	zend_hash_clean(&collector->symbol_tables);

	for (uint32_t node = 0; node < collector->node_count; node++) {
		const collector_node_t *entry = &collector->nodes[node];

		if (UNEXPECTED(entry->flags & COLLECTOR_NODE_REACH)) {
			if (UNEXPECTED(entry->flags & COLLECTOR_NODE_KNOWN_LIVE)) {
				collector_mark_live(collector, node);
			}

			continue;
		}

		const uint32_t refcount = collector_node_refcount(entry);

		if (UNEXPECTED(refcount < entry->internal)) {
			ZEND_ASSERT(0 && "a reporter gave a reference its holder does not own");
			return false;
		}

		if (UNEXPECTED((entry->flags & COLLECTOR_NODE_KNOWN_LIVE) || refcount > entry->internal)) {
			collector_mark_live(collector, node);
		}
	}

	while (collector->worklist_count != 0) {
		const uint32_t node = collector->worklist[--collector->worklist_count];

		for (uint32_t edge = collector->nodes[node].first_edge; edge != COLLECTOR_NONE;
			 edge = collector->edges[edge].next) {
			collector_mark_live(collector, collector->edges[edge].to);
		}

		if (EXPECTED(!(collector->nodes[node].flags & COLLECTOR_NODE_KNOWN_LIVE))) {
			collector_node_references(collector, node);
		}
	}

	return !collector->failed;
}

async_coroutine_t **async_collector_find(uint32_t *count, const size_t ceiling)
{
	async_collector_t collector;
	async_coroutine_t *coroutine = NULL;
	uint32_t candidate_count = 0;

	*count = 0;

	/* The request's end calls every destructor not yet called, whatever holds its object
	 * (zend_objects_store_call_destructors): a route into any subgraph that no reference counts. */
	if (UNEXPECTED(EG(flags) & EG_FLAGS_IN_SHUTDOWN)) {
		return NULL;
	}

	ZEND_HASH_FOREACH_PTR(&ASYNC_G(coroutines), coroutine)
	{
		if (EXPECTED(!collector_is_candidate(coroutine))) {
			continue;
		}

		if (UNEXPECTED(candidate_count == 0)) {
			collector_init(&collector);
			collector.ceiling = ceiling;
		}

		if (UNEXPECTED(!collector_has_node_room(&collector))) {
			collector_destroy(&collector);
			return NULL;
		}

		/* A WeakReference or a WeakMap in running code may still reach it. */
		const uint8_t flags = COLLECTOR_NODE_CANDIDATE |
				((GC_FLAGS(&coroutine->std) & IS_OBJ_WEAKLY_REFERENCED) ? COLLECTOR_NODE_KNOWN_LIVE : 0);
		zval position;

		ZVAL_LONG(&position, collector_node_add(&collector, (zend_refcounted *) &coroutine->std, flags));
		zend_hash_index_add_new(&collector.index, collector_key((zend_refcounted *) &coroutine->std), &position);

		/* The registry's reference (S7.md section 2). */
		collector.nodes[Z_LVAL(position)].internal = 1;
		candidate_count++;
	}
	ZEND_HASH_FOREACH_END();

	if (EXPECTED(candidate_count == 0)) {
		return NULL;
	}

	for (uint32_t candidate = 0; candidate < candidate_count; candidate++) {
		async_scope_collector_reach(
				&collector, async_coroutine_from_object((zend_object *) collector.nodes[candidate].address), candidate);
	}

#ifndef PHP_WIN32
	async_signal_collector_seed(&collector);
#endif
	collector_wake_edges(&collector, candidate_count);
	collector_count(&collector);
	collector_holders(&collector);

	if (UNEXPECTED(collector.failed || !collector_spread(&collector))) {
		collector_destroy(&collector);
		return NULL;
	}

	async_coroutine_t **found = safe_emalloc(candidate_count, sizeof(*found), 0);

	for (uint32_t candidate = 0; candidate < candidate_count; candidate++) {
		if (EXPECTED(collector.nodes[candidate].flags & COLLECTOR_NODE_LIVE)) {
			continue;
		}

		coroutine = async_coroutine_from_object((zend_object *) collector.nodes[candidate].address);
		found[(*count)++] = coroutine;
#ifdef TRUE_ASYNC_TEST_HOOKS
		coroutine->coroutine.flags |= ASYNC_COROUTINE_F_DEADLOCK_FOUND;
#endif
	}

	collector_destroy(&collector);

	if (EXPECTED(*count == 0)) {
		efree(found);
		return NULL;
	}

	return found;
}

///////////////////////////////////////////////////////////////////
/// The automatic run
///////////////////////////////////////////////////////////////////

/* The automatic run backs off to 64 times the interval while it finds nothing new. */
#define COLLECTOR_BACKOFF_MAX 6

static void collector_first_record(const async_coroutine_event_callback_t *record, void *arg)
{
	const async_coroutine_event_callback_t **first = arg;

	if (EXPECTED(*first == NULL)) {
		*first = record;
	}
}

/* S7.md section 6: raised in scheduler context, at the coroutine's suspend location, when the INI
 * error_reporting has E_WARNING: the scheduler's stack keeps its own copy of the setting. Nothing
 * called what warns, so an exception an error handler throws is released. True when it warned. */
static bool collector_warn(async_coroutine_t *coroutine)
{
	const zend_long error_reporting = async_ini_error_reporting();

	if (UNEXPECTED(!(error_reporting & E_WARNING))) {
		return false;
	}

	const async_coroutine_event_callback_t *first_record = NULL;
	async_wait_walk(coroutine, collector_first_record, &first_record);

	zend_string *waits_for = first_record->event_callback.kind->info(first_record);
	const zend_string *spawn_file = coroutine->coroutine.filename;
	/* No file: main, or a spawn while no PHP code ran. */
	zend_string *spawned_at = EXPECTED(spawn_file != NULL)
			? zend_strpprintf(0, " spawned at %s:%" PRIu32, ZSTR_VAL(spawn_file), coroutine->coroutine.lineno)
			: ZSTR_EMPTY_ALLOC();
	const zend_execute_data *suspend_frame = async_coroutine_suspend_frame(coroutine);
	const int saved_error_reporting = EG(error_reporting);

	EG(error_reporting) = (int) error_reporting;

#define COLLECTOR_WARNING "Partial deadlock: coroutine #%u%s can never wake (%s)"
	if (EXPECTED(suspend_frame != NULL)) {
		zend_error_at(E_WARNING,
					  suspend_frame->func->op_array.filename,
					  suspend_frame->opline->lineno,
					  COLLECTOR_WARNING,
					  coroutine->std.handle,
					  ZSTR_VAL(spawned_at),
					  ZSTR_VAL(waits_for));
	} else {
		zend_error(E_WARNING, COLLECTOR_WARNING, coroutine->std.handle, ZSTR_VAL(spawned_at), ZSTR_VAL(waits_for));
	}
#undef COLLECTOR_WARNING

	EG(error_reporting) = saved_error_reporting;
	zend_string_release(spawned_at);
	zend_string_release(waits_for);

	/* An exit() in the handler ends the request as in a coroutine (D16). */
	if (UNEXPECTED(EG(exception) != NULL)) {
		const bool is_exit = zend_is_unwind_exit(EG(exception));

		zend_clear_exception();

		if (UNEXPECTED(is_exit)) {
			async_scheduler_cancel_for_exit();
		}
	}

	return true;
}

/* Whether `coroutine` is still parked where the run found it: an earlier warning's handler may have
 * ended its wait. */
static zend_always_inline bool collector_still_parked(async_coroutine_t *coroutine)
{
	return ZEND_COROUTINE_IS_SUSPENDED(&coroutine->coroutine) && !async_wait_is_empty(coroutine);
}

/* One warning per coroutine, ever (ASYNC_COROUTINE_F_DEADLOCK_REPORTED). True when it warned. */
static bool collector_report(async_coroutine_t **found, const uint32_t count)
{
	bool warned = false;

	for (uint32_t i = 0; i < count; i++) {
		async_coroutine_t *coroutine = found[i];
		zend_coroutine_t *zend_coroutine = &coroutine->coroutine;

		if (EXPECTED(!(zend_coroutine->flags & ASYNC_COROUTINE_F_DEADLOCK_REPORTED) &&
					 collector_still_parked(coroutine)) &&
			EXPECTED(collector_warn(coroutine))) {
			zend_coroutine->flags |= ASYNC_COROUTINE_F_DEADLOCK_REPORTED;
			warned = true;
		}
	}

	return warned;
}

/* `cancel` (S7.md 6): each coroutine still parked is cancelled as the global deadlock cancels its
 * waiters, protection cleared. True when it cancelled one; `*first` when one of them had never been
 * cancelled before, which is what resets the back-off. */
static bool collector_cancel(async_coroutine_t **found, const uint32_t count, bool *first)
{
	bool cancelled = false;
	zend_coroutine_t *found_main = NULL;

	for (uint32_t i = 0; i < count; i++) {
		async_coroutine_t *coroutine = found[i];
		zend_coroutine_t *zend_coroutine = &coroutine->coroutine;

		/* Main's uncaught cancellation would end the script silently with status 0; parked, main
		 * meets the global deadlock and its DeadlockError once the rest of the request ends. */
		if (UNEXPECTED(ZEND_COROUTINE_IS_MAIN(zend_coroutine))) {
			found_main = zend_coroutine;
			continue;
		}

		if (UNEXPECTED(!collector_still_parked(coroutine))) {
			continue;
		}

		if (EXPECTED(!ZEND_COROUTINE_IS_CANCELLED(zend_coroutine))) {
			*first = true;
		}

		/* As registry_cancel() in scheduler.c: no holder cancels it, so the oracle takes it as handed
		 * out. */
		zend_coroutine->flags &= ~ASYNC_COROUTINE_F_PROTECTED;
#ifdef TRUE_ASYNC_TEST_HOOKS
		zend_coroutine->flags |= ASYNC_COROUTINE_F_HANDED_OUT;
#endif
		async_coroutine_cancel(coroutine, async_new_exception(async_ce_cancellation, "Deadlock detected"), true, false);
		cancelled = true;
	}

#ifdef TRUE_ASYNC_TEST_HOOKS
	/* The coroutines cancelled here run their cleanup, which may wake main. */
	if (UNEXPECTED(found_main != NULL && cancelled)) {
		found_main->flags |= ASYNC_COROUTINE_F_HANDED_OUT;
	}
#else
	(void) found_main;
#endif

	return cancelled;
}

bool async_collector_idle(void)
{
	if (UNEXPECTED(ASYNC_G(partial_deadlock) == ASYNC_PARTIAL_DEADLOCK_OFF)) {
		return false;
	}

	const zend_long interval = ASYNC_G(partial_deadlock_interval);
	const uint64_t now = zend_hrtime();

	if (EXPECTED(interval != 0)) {
		/* The first idle point starts the clock. */
		if (UNEXPECTED(ASYNC_G(collector_last_run) == 0)) {
			ASYNC_G(collector_last_run) = now;
			return false;
		}

		const uint64_t due = ((uint64_t) interval * (ZEND_NANO_IN_SEC / 1000)) << ASYNC_G(collector_backoff);

		if (EXPECTED(now - ASYNC_G(collector_last_run) < due)) {
			return false;
		}
	}

	ASYNC_G(collector_last_run) = now;

	uint32_t count = 0;
	/* memory_limit is -1 when unlimited. */
	async_coroutine_t **found = async_collector_find(&count, PG(memory_limit) > 0 ? (size_t) PG(memory_limit) : 0);
	bool warned = false;
	bool cancelled = false;
	bool first_cancel = false;

	if (UNEXPECTED(found != NULL)) {
		/* An error handler runs PHP code between the warnings: each coroutine is held across them. */
		for (uint32_t i = 0; i < count; i++) {
			GC_ADDREF(&found[i]->std);
		}

		warned = collector_report(found, count);

		if (ASYNC_G(partial_deadlock) == ASYNC_PARTIAL_DEADLOCK_CANCEL) {
			cancelled = collector_cancel(found, count, &first_cancel);
		}

		for (uint32_t i = 0; i < count; i++) {
			OBJ_RELEASE(&found[i]->std);
		}

		efree(found);
	}

	if (UNEXPECTED(warned || first_cancel)) {
		ASYNC_G(collector_backoff) = 0;
	} else if (EXPECTED(interval != 0 && ASYNC_G(collector_backoff) < COLLECTOR_BACKOFF_MAX)) {
		ASYNC_G(collector_backoff)++;
	}

	return warned || cancelled;
}

void async_collector_request_startup(void)
{
	ASYNC_G(collector_last_run) = 0;
	ASYNC_G(collector_backoff) = 0;
}

#ifdef TRUE_ASYNC_TEST_HOOKS
void async_collector_check_wake(async_coroutine_t *waiter, const async_coroutine_t *target)
{
	/* What woke the target may have come through a coroutine handed out, or through the bailout that
	 * ends every coroutine; so may whatever the waiter's own waiters see. */
	if (UNEXPECTED(target->coroutine.flags & (ASYNC_COROUTINE_F_HANDED_OUT | ASYNC_COROUTINE_F_BAILOUT))) {
		waiter->coroutine.flags |= ASYNC_COROUTINE_F_HANDED_OUT;
		return;
	}

	if (EXPECTED((waiter->coroutine.flags & (ASYNC_COROUTINE_F_DEADLOCK_FOUND | ASYNC_COROUTINE_F_HANDED_OUT)) !=
				 ASYNC_COROUTINE_F_DEADLOCK_FOUND)) {
		return;
	}

	fprintf(stderr,
			"true_async collector: coroutine #%u was found never to wake, and coroutine #%u woke it\n",
			waiter->std.handle,
			target->std.handle);
	fflush(stderr);
	abort();
}

void async_collector_check_event_wake(async_coroutine_t *waiter)
{
	const async_coroutine_t *completer = (const async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;

	if (UNEXPECTED(completer != NULL && !ZEND_ASYNC_IN_SCHEDULER_CONTEXT &&
				   (completer->coroutine.flags & ASYNC_COROUTINE_F_BAILOUT))) {
		waiter->coroutine.flags |= ASYNC_COROUTINE_F_HANDED_OUT;
		return;
	}

	if (EXPECTED((waiter->coroutine.flags & (ASYNC_COROUTINE_F_DEADLOCK_FOUND | ASYNC_COROUTINE_F_HANDED_OUT)) !=
				 ASYNC_COROUTINE_F_DEADLOCK_FOUND)) {
		return;
	}

	fprintf(stderr,
			"true_async collector: coroutine #%u was found never to wake, and an event it waits for completed\n",
			waiter->std.handle);
	fflush(stderr);
	abort();
}

void async_collector_check_cancel(async_coroutine_t *coroutine)
{
	const uint32_t excused = ASYNC_COROUTINE_F_HANDED_OUT | ASYNC_COROUTINE_F_BAILOUT;
	const uint32_t flags = coroutine->coroutine.flags & (ASYNC_COROUTINE_F_DEADLOCK_FOUND | excused);

	if (EXPECTED(flags != ASYNC_COROUTINE_F_DEADLOCK_FOUND)) {
		return;
	}

	fprintf(stderr,
			"true_async collector: coroutine #%u was found never to wake, and code that holds it cancelled it\n",
			coroutine->std.handle);
	fflush(stderr);
	abort();
}
#endif
