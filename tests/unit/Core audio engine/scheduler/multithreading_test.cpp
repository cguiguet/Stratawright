#include <catch2/catch_test_macros.hpp>
#include "Core audio engine/scheduler/scheduler_impl.h"
#include "Core infrastructure/bridges/imutation_bridge.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>

using namespace Layer3;

TEST_CASE("Scheduler: Multithreading Handoff", "[Layer3][Scheduler][MT]") {
    auto mutationBridge = Layer2::IMutationBridge::create(16384);
    auto kernel = IDSPKernel::create(1024);
    REQUIRE(kernel != nullptr);

    kernel->registerProcessor(0, [](NodeID, float* const*, float* const*, uint32_t, uint32_t, const EventData*, uint32_t, EventData*, uint32_t*, const ProcessContext*, const bool*, bool*) {});
    kernel->attachMutationBridge(mutationBridge.get());

    SECTION("Topology Version Monotonicity (10,000 Mutations)") {
        const int numMutations = 10000;
        
        for (int i = 0; i < numMutations; ++i) {
            SystemMutation mutation = {};
            mutation.type = Layer2::MutationType::NODE_ADD;
            mutation.node.id = {static_cast<uint32_t>(i + 1), 1};
            mutation.node.type = 0;
            mutationBridge->pushMutation(mutation);
        }
        
        // Wait for worker to catch up (node count is the true indicator of completion)
        for (int retry = 0; retry < 500; ++retry) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ProcessContext MT_TEST_CONTEXT = {};
            MT_TEST_CONTEXT.currentBlockSize = 512; MT_TEST_CONTEXT.sampleRate = 48000.0f;
            kernel->process(nullptr, nullptr, 2, 512, &MT_TEST_CONTEXT);
            if (kernel->getNodeCount() == static_cast<uint32_t>(numMutations)) {
                break;
            }
        }
        CHECK(kernel->getTopologyVersion() > 0);
        CHECK(kernel->getNodeCount() == static_cast<uint32_t>(numMutations));
    }

    SECTION("Starvation and Wait-Free Swap (3 cycles skipped)") {
        uint32_t versionBefore = kernel->getTopologyVersion();
        
        // Push 3 new topologies (as mutation batches)
        for (int i = 0; i < 3; ++i) {
            SystemMutation mutation;
            mutation.type = Layer2::MutationType::NODE_ADD;
            mutation.node.id = {static_cast<uint32_t>(i + 11000), 1};
            mutation.node.type = 0;
            mutationBridge->pushMutation(mutation);
            
            // Wait for worker to rebuild each
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // The RT thread has NOT called process() yet, so it hasn't swapped.
        // Now call process once. It should pick up the LATEST topology immediately.
        ProcessContext MT_TEST_CONTEXT = {};
        kernel->process(nullptr, nullptr, 2, 512, &MT_TEST_CONTEXT);
        
        uint32_t versionAfter = kernel->getTopologyVersion();
        CHECK(versionAfter > versionBefore);
        
        // Verify that we can still process and everything is stable
        kernel->process(nullptr, nullptr, 2, 512, &MT_TEST_CONTEXT);
        CHECK(kernel->getTopologyVersion() == versionAfter);
    }
}


namespace {
std::atomic<bool> gBlockEntered{false};
std::atomic<bool> gBlockReleased{false};
std::atomic<bool> gBlockFinished{false};
}

TEST_CASE("Scheduler: waiting for the blocks in flight", "[Layer3][Scheduler][MT]") {
    auto mutationBridge = Layer2::IMutationBridge::create(64);
    auto kernel = IDSPKernel::create(16);
    REQUIRE(kernel != nullptr);

    // A node whose processing holds the block until the test releases it.
    gBlockEntered = false;
    gBlockReleased = true;
    gBlockFinished = false;
    kernel->registerProcessor(0, [](NodeID, float* const*, float* const*, uint32_t, uint32_t, const EventData*, uint32_t, EventData*, uint32_t*, const ProcessContext*, const bool*, bool*) {
        gBlockEntered = true;
        while (!gBlockReleased) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        gBlockFinished = true;
    });
    kernel->attachMutationBridge(mutationBridge.get());

    SystemMutation mutation = {};
    mutation.type = Layer2::MutationType::NODE_ADD;
    mutation.node.id = {1, 1};
    mutation.node.type = 0;
    mutationBridge->pushMutation(mutation);
    ProcessContext context = {};
    for (int retry = 0; retry < 500 && !gBlockEntered; ++retry) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        kernel->process(nullptr, nullptr, 2, 64, &context);
    }
    REQUIRE(gBlockEntered);

    // Nothing running: returns at once.
    CHECK(kernel->waitForBlocksInFlight(0));

    gBlockEntered = false;
    gBlockReleased = false;
    gBlockFinished = false;
    std::thread audioThread([&] { kernel->process(nullptr, nullptr, 2, 64, &context); });
    while (!gBlockEntered) {
        std::this_thread::yield();
    }

    // The block is held: the wait times out.
    CHECK_FALSE(kernel->waitForBlocksInFlight(20));
    CHECK_FALSE(gBlockFinished);

    // Released shortly: the wait returns only once the block has finished.
    std::thread releaser([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        gBlockReleased = true;
    });
    CHECK(kernel->waitForBlocksInFlight(2000));
    CHECK(gBlockFinished);

    releaser.join();
    audioThread.join();
}
