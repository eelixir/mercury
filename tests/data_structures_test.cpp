#include <gtest/gtest.h>
#include "IntrusiveList.h"
#include "HashMap.h"
#include <string>
#include <vector>
#include <algorithm>

using namespace Mercury;

// ============== IntrusiveList Tests ==============

// Test node that inherits from IntrusiveListNode
struct TestNode : public IntrusiveListNode<TestNode> {
    int id;
    int value;

    TestNode(int i = 0, int v = 0) : id(i), value(v) {}
};

class IntrusiveListTest : public ::testing::Test {
protected:
    IntrusiveList<TestNode> list;
    std::vector<TestNode> nodes;

    void SetUp() override {
        nodes.resize(10);
        for (int i = 0; i < 10; ++i) {
            nodes[i] = TestNode(i, i * 100);
        }
    }

    void TearDown() override {
        list.clear();
    }
};

TEST_F(IntrusiveListTest, EmptyList) {
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.size(), 0);
}

TEST_F(IntrusiveListTest, PushBack) {
    list.push_back(&nodes[0]);
    EXPECT_FALSE(list.empty());
    EXPECT_EQ(list.size(), 1);
    EXPECT_EQ(list.front().id, 0);
    EXPECT_EQ(list.back().id, 0);
}

TEST_F(IntrusiveListTest, PushFront) {
    list.push_front(&nodes[0]);
    EXPECT_EQ(list.size(), 1);
    EXPECT_EQ(list.front().id, 0);

    list.push_front(&nodes[1]);
    EXPECT_EQ(list.size(), 2);
    EXPECT_EQ(list.front().id, 1);
    EXPECT_EQ(list.back().id, 0);
}

TEST_F(IntrusiveListTest, PushBackMultiple) {
    for (int i = 0; i < 5; ++i) {
        list.push_back(&nodes[i]);
    }

    EXPECT_EQ(list.size(), 5);
    EXPECT_EQ(list.front().id, 0);
    EXPECT_EQ(list.back().id, 4);

    // Verify order through iteration
    int expected = 0;
    for (auto& node : list) {
        EXPECT_EQ(node.id, expected++);
    }
}

TEST_F(IntrusiveListTest, PopFront) {
    list.push_back(&nodes[0]);
    list.push_back(&nodes[1]);
    list.push_back(&nodes[2]);

    list.pop_front();
    EXPECT_EQ(list.size(), 2);
    EXPECT_EQ(list.front().id, 1);

    list.pop_front();
    EXPECT_EQ(list.size(), 1);
    EXPECT_EQ(list.front().id, 2);

    list.pop_front();
    EXPECT_TRUE(list.empty());
}

TEST_F(IntrusiveListTest, PopBack) {
    list.push_back(&nodes[0]);
    list.push_back(&nodes[1]);
    list.push_back(&nodes[2]);

    list.pop_back();
    EXPECT_EQ(list.size(), 2);
    EXPECT_EQ(list.back().id, 1);

    list.pop_back();
    EXPECT_EQ(list.size(), 1);
    EXPECT_EQ(list.back().id, 0);
}

TEST_F(IntrusiveListTest, RemoveMiddle) {
    list.push_back(&nodes[0]);
    list.push_back(&nodes[1]);
    list.push_back(&nodes[2]);

    list.remove(&nodes[1]);
    EXPECT_EQ(list.size(), 2);
    EXPECT_EQ(list.front().id, 0);
    EXPECT_EQ(list.back().id, 2);

    // Verify node is unlinked
    EXPECT_FALSE(nodes[1].isLinked());
}

TEST_F(IntrusiveListTest, RemoveFirst) {
    list.push_back(&nodes[0]);
    list.push_back(&nodes[1]);
    list.push_back(&nodes[2]);

    list.remove(&nodes[0]);
    EXPECT_EQ(list.size(), 2);
    EXPECT_EQ(list.front().id, 1);
}

TEST_F(IntrusiveListTest, RemoveLast) {
    list.push_back(&nodes[0]);
    list.push_back(&nodes[1]);
    list.push_back(&nodes[2]);

    list.remove(&nodes[2]);
    EXPECT_EQ(list.size(), 2);
    EXPECT_EQ(list.back().id, 1);
}

TEST_F(IntrusiveListTest, InsertAfter) {
    list.push_back(&nodes[0]);
    list.push_back(&nodes[2]);

    list.insert_after(&nodes[0], &nodes[1]);
    
    EXPECT_EQ(list.size(), 3);
    
    auto it = list.begin();
    EXPECT_EQ(it->id, 0); ++it;
    EXPECT_EQ(it->id, 1); ++it;
    EXPECT_EQ(it->id, 2);
}

TEST_F(IntrusiveListTest, InsertBefore) {
    list.push_back(&nodes[0]);
    list.push_back(&nodes[2]);

    list.insert_before(&nodes[2], &nodes[1]);
    
    EXPECT_EQ(list.size(), 3);
    
    auto it = list.begin();
    EXPECT_EQ(it->id, 0); ++it;
    EXPECT_EQ(it->id, 1); ++it;
    EXPECT_EQ(it->id, 2);
}

TEST_F(IntrusiveListTest, Clear) {
    for (int i = 0; i < 5; ++i) {
        list.push_back(&nodes[i]);
    }

    list.clear();
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.size(), 0);

    // Verify all nodes are unlinked
    for (int i = 0; i < 5; ++i) {
        EXPECT_FALSE(nodes[i].isLinked());
    }
}

TEST_F(IntrusiveListTest, IteratorTraversal) {
    for (int i = 0; i < 5; ++i) {
        list.push_back(&nodes[i]);
    }

    std::vector<int> collected;
    for (const auto& node : list) {
        collected.push_back(node.id);
    }

    EXPECT_EQ(collected, (std::vector<int>{0, 1, 2, 3, 4}));
}

TEST_F(IntrusiveListTest, MoveConstructor) {
    list.push_back(&nodes[0]);
    list.push_back(&nodes[1]);

    IntrusiveList<TestNode> other(std::move(list));

    EXPECT_TRUE(list.empty());
    EXPECT_EQ(other.size(), 2);
    EXPECT_EQ(other.front().id, 0);
}

// ============== HashMap Tests ==============

class HashMapTest : public ::testing::Test {
protected:
    HashMap<uint64_t, int, OrderIdHash> map;
};

TEST_F(HashMapTest, EmptyMap) {
    EXPECT_TRUE(map.empty());
    EXPECT_EQ(map.size(), 0);
}

TEST_F(HashMapTest, InsertAndFind) {
    map.insert(1, 100);
    
    EXPECT_EQ(map.size(), 1);
    EXPECT_FALSE(map.empty());
    
    int* value = map.find(1);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 100);
}

TEST_F(HashMapTest, InsertMultiple) {
    for (uint64_t i = 0; i < 100; ++i) {
        map.insert(i, static_cast<int>(i * 10));
    }

    EXPECT_EQ(map.size(), 100);

    for (uint64_t i = 0; i < 100; ++i) {
        int* value = map.find(i);
        ASSERT_NE(value, nullptr);
        EXPECT_EQ(*value, static_cast<int>(i * 10));
    }
}

TEST_F(HashMapTest, FindNonExistent) {
    map.insert(1, 100);
    
    int* value = map.find(2);
    EXPECT_EQ(value, nullptr);
}

TEST_F(HashMapTest, Contains) {
    map.insert(42, 999);
    
    EXPECT_TRUE(map.contains(42));
    EXPECT_FALSE(map.contains(43));
}

TEST_F(HashMapTest, UpdateExisting) {
    map.insert(1, 100);
    map.insert(1, 200);  // Update

    EXPECT_EQ(map.size(), 1);  // Still only one entry
    EXPECT_EQ(*map.find(1), 200);
}

TEST_F(HashMapTest, OperatorBracket) {
    map[1] = 100;
    map[2] = 200;

    EXPECT_EQ(map[1], 100);
    EXPECT_EQ(map[2], 200);

    map[1] = 150;  // Update
    EXPECT_EQ(map[1], 150);
}

TEST_F(HashMapTest, Erase) {
    map.insert(1, 100);
    map.insert(2, 200);
    map.insert(3, 300);

    EXPECT_TRUE(map.erase(2));
    EXPECT_EQ(map.size(), 2);
    EXPECT_FALSE(map.contains(2));
    EXPECT_TRUE(map.contains(1));
    EXPECT_TRUE(map.contains(3));
}

TEST_F(HashMapTest, EraseNonExistent) {
    map.insert(1, 100);
    
    EXPECT_FALSE(map.erase(2));
    EXPECT_EQ(map.size(), 1);
}

TEST_F(HashMapTest, Clear) {
    for (uint64_t i = 0; i < 50; ++i) {
        map.insert(i, static_cast<int>(i));
    }

    map.clear();
    EXPECT_TRUE(map.empty());
    EXPECT_EQ(map.size(), 0);
}

TEST_F(HashMapTest, GetOptional) {
    map.insert(1, 100);

    auto val1 = map.get(1);
    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(val1.value(), 100);

    auto val2 = map.get(2);
    EXPECT_FALSE(val2.has_value());
}

TEST_F(HashMapTest, IterateAll) {
    map.insert(1, 10);
    map.insert(2, 20);
    map.insert(3, 30);

    int sum = 0;
    int count = 0;
    for (auto it = map.begin(); it != map.end(); ++it) {
        sum += it.value();
        ++count;
    }

    EXPECT_EQ(count, 3);
    EXPECT_EQ(sum, 60);
}

TEST_F(HashMapTest, GrowthAndRehash) {
    // Insert enough elements to trigger multiple rehashes
    for (uint64_t i = 0; i < 1000; ++i) {
        map.insert(i, static_cast<int>(i));
    }

    EXPECT_EQ(map.size(), 1000);

    // Verify all elements still accessible
    for (uint64_t i = 0; i < 1000; ++i) {
        ASSERT_NE(map.find(i), nullptr);
        EXPECT_EQ(*map.find(i), static_cast<int>(i));
    }
}

TEST_F(HashMapTest, LoadFactor) {
    EXPECT_FLOAT_EQ(map.loadFactor(), 0.0f);

    map.insert(1, 1);
    EXPECT_GT(map.loadFactor(), 0.0f);
    EXPECT_LE(map.loadFactor(), 1.0f);
}

TEST_F(HashMapTest, Reserve) {
    map.reserve(1000);
    EXPECT_GE(map.capacity(), 1000);

    // Insert should still work
    for (uint64_t i = 0; i < 500; ++i) {
        map.insert(i, static_cast<int>(i));
    }
    EXPECT_EQ(map.size(), 500);
}

TEST_F(HashMapTest, MoveConstructor) {
    map.insert(1, 100);
    map.insert(2, 200);

    HashMap<uint64_t, int, OrderIdHash> other(std::move(map));

    EXPECT_TRUE(map.empty());
    EXPECT_EQ(other.size(), 2);
    EXPECT_EQ(*other.find(1), 100);
}

TEST_F(HashMapTest, StringKeyValue) {
    HashMap<std::string, std::string> strMap;

    strMap.insert("hello", "world");
    strMap.insert("foo", "bar");

    EXPECT_EQ(*strMap.find("hello"), "world");
    EXPECT_EQ(*strMap.find("foo"), "bar");
}

TEST_F(HashMapTest, EraseWithCollisions) {
    // Insert many items that may have collisions
    for (uint64_t i = 0; i < 100; ++i) {
        map.insert(i * 16, static_cast<int>(i));  // May cause clustering
    }

    // Erase some in the middle
    for (uint64_t i = 25; i < 75; ++i) {
        EXPECT_TRUE(map.erase(i * 16));
    }

    EXPECT_EQ(map.size(), 50);

    // Verify remaining elements
    for (uint64_t i = 0; i < 25; ++i) {
        EXPECT_TRUE(map.contains(i * 16));
    }
    for (uint64_t i = 75; i < 100; ++i) {
        EXPECT_TRUE(map.contains(i * 16));
    }
}

// ============== Performance Sanity Check ==============

TEST(PerformanceTest, IntrusiveListOperations) {
    const int N = 10000;
    std::vector<TestNode> nodes(N);
    IntrusiveList<TestNode> list;

    // Push back all
    for (int i = 0; i < N; ++i) {
        nodes[i] = TestNode(i, i);
        list.push_back(&nodes[i]);
    }
    EXPECT_EQ(list.size(), N);

    // Remove every other
    for (int i = 0; i < N; i += 2) {
        list.remove(&nodes[i]);
    }
    EXPECT_EQ(list.size(), N / 2);

    list.clear();
}

TEST(PerformanceTest, HashMapOperations) {
    const int N = 100000;
    HashMap<uint64_t, int, OrderIdHash> map;

    // Insert all
    for (int i = 0; i < N; ++i) {
        map.insert(static_cast<uint64_t>(i), i);
    }
    EXPECT_EQ(map.size(), N);

    // Find all
    for (int i = 0; i < N; ++i) {
        ASSERT_NE(map.find(static_cast<uint64_t>(i)), nullptr);
    }

    // Erase half
    for (int i = 0; i < N; i += 2) {
        map.erase(static_cast<uint64_t>(i));
    }
    EXPECT_EQ(map.size(), N / 2);
}
