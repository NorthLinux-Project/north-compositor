#include "unity.h"
#include "util/arena.h"

void setUp(void) {}
void tearDown(void) {}

void test_arena_creation_and_basic_allocation(void)
{
    Arena arena = arena_create(1024);
    TEST_ASSERT_NOT_NULL(arena.buffer);
    TEST_ASSERT_EQUAL_size_t(1024, arena.capacity);
    TEST_ASSERT_EQUAL_size_t(0, arena.offset);

    void *ptr1 = arena_alloc(&arena, 64, 8);
    TEST_ASSERT_NOT_NULL(ptr1);
    TEST_ASSERT_EQUAL_size_t(64, arena.offset);

    void *ptr2 = arena_alloc(&arena, 128, 16);
    TEST_ASSERT_NOT_NULL(ptr2);
    TEST_ASSERT_TRUE(arena.offset >= 64 + 128);

    arena_destroy(&arena);
    TEST_ASSERT_NULL(arena.buffer);
}

void test_arena_alignment_guarantees(void)
{
    Arena arena = arena_create(1024);

    arena_alloc(&arena, 3, 1);
    void *aligned_16 = arena_alloc(&arena, 8, 16);
    TEST_ASSERT_NOT_NULL(aligned_16);
    TEST_ASSERT_EQUAL_UINT(0, ((uintptr_t)aligned_16) % 16);

    void *aligned_64 = arena_alloc(&arena, 8, 64);
    TEST_ASSERT_NOT_NULL(aligned_64);
    TEST_ASSERT_EQUAL_UINT(0, ((uintptr_t)aligned_64) % 64);

    arena_destroy(&arena);
}

void test_arena_out_of_memory_returns_null(void)
{
    Arena arena = arena_create(128);
    void *ptr1 = arena_alloc(&arena, 100, 8);
    TEST_ASSERT_NOT_NULL(ptr1);

    void *ptr2 = arena_alloc(&arena, 50, 8);
    TEST_ASSERT_NULL(ptr2);

    arena_destroy(&arena);
}

void test_arena_temp_scope_rollback(void)
{
    Arena arena = arena_create(1024);
    arena_alloc(&arena, 100, 8);
    size_t offset_before = arena.offset;

    ArenaTemp temp = arena_temp_begin(&arena);
    void *tmp_ptr = arena_alloc(&arena, 200, 8);
    TEST_ASSERT_NOT_NULL(tmp_ptr);
    TEST_ASSERT_TRUE(arena.offset > offset_before);

    arena_temp_end(temp);
    TEST_ASSERT_EQUAL_size_t(offset_before, arena.offset);

    arena_destroy(&arena);
}

void test_scratch_arena_pool_and_conflict(void)
{
    ArenaTemp scratch1 = get_scratch(NULL, 0);
    TEST_ASSERT_NOT_NULL(scratch1.arena);

    Arena *conflicts[] = {scratch1.arena};
    ArenaTemp scratch2 = get_scratch(conflicts, 1);
    TEST_ASSERT_NOT_NULL(scratch2.arena);
    TEST_ASSERT_TRUE(scratch1.arena != scratch2.arena);

    arena_temp_end(scratch2);
    arena_temp_end(scratch1);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_arena_creation_and_basic_allocation);
    RUN_TEST(test_arena_alignment_guarantees);
    RUN_TEST(test_arena_out_of_memory_returns_null);
    RUN_TEST(test_arena_temp_scope_rollback);
    RUN_TEST(test_scratch_arena_pool_and_conflict);
    return UNITY_END();
}
