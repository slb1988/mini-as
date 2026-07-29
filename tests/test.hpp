#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

struct TestCase { std::string name; std::function<void()> run; };
std::vector<TestCase>& TestRegistry();

struct TestRegistration {
    TestRegistration(const char* name, std::function<void()> run);
};

#define TEST_CASE(name) \
    static void name(); \
    static TestRegistration registration_##name(#name, name); \
    static void name()

#define CHECK(condition) \
    do { if (!(condition)) throw std::runtime_error("check failed: " #condition); } while (false)

