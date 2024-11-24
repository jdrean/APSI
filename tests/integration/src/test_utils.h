// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#pragma once

// STD
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// APSI
#include "apsi/item.h"
#include "apsi/psi_params.h"
#include "apsi/receiver.h"

// Google Test
//#include "gtest/gtest.h"

namespace APSITests {
    apsi::Label create_label(unsigned char start, std::size_t byte_count);

    std::unordered_set<apsi::Item> rand_subset(
        const std::unordered_set<apsi::Item> &items, std::size_t size);

    std::unordered_set<apsi::Item> rand_subset(
        const std::unordered_map<apsi::Item, apsi::Label> &item_labels, std::size_t size);

    std::vector<apsi::Item> rand_subset(const std::vector<apsi::Item> &items, std::size_t size);

    std::vector<apsi::Item> rand_subset(
        const std::vector<std::pair<apsi::Item, apsi::Label>> &items, std::size_t size);

    void verify_unlabeled_results(
        const std::vector<apsi::receiver::MatchRecord> &query_result,
        const std::vector<apsi::Item> &query_vec,
        const std::vector<apsi::Item> &int_items);

    void verify_labeled_results(
        const std::vector<apsi::receiver::MatchRecord> &query_result,
        const std::vector<apsi::Item> &query_vec,
        const std::vector<apsi::Item> &int_items,
        const std::vector<std::pair<apsi::Item, apsi::Label>> &all_item_labels);

    apsi::PSIParams create_params1();

    apsi::PSIParams create_params2();

    apsi::PSIParams create_huge_params1();

    apsi::PSIParams create_huge_params2();
} // namespace APSITests

struct CycleAccumulator {
    uint64_t oprf_receiver_creation_cycles = 0;
    uint64_t oprf_request_creation_cycles = 0;
    uint64_t send_oprf_request_cycles = 0;
    uint64_t receive_oprf_request_cycles = 0;
    uint64_t run_oprf_cycles = 0;
    uint64_t receive_oprf_response_cycles = 0;
    uint64_t extract_hashes_cycles = 0;
    uint64_t create_query_cycles = 0;
    uint64_t send_query_cycles = 0;
    uint64_t receive_query_cycles = 0;
    uint64_t run_query_cycles = 0;
    uint64_t receive_query_response_cycles = 0;
    uint64_t process_result_cycles = 0;
    uint64_t hash_oprf_request_cycles = 0;
    uint64_t hash_oprf_response_cycles = 0;
    uint64_t hash_received_query_cycles = 0;
    uint64_t hash_query_response_cycles = 0;
    uint64_t hash_final_result_cycles = 0;

    // Number of runs to calculate average
    size_t run_count = 0;

    // Method to accumulate cycles from a single run
    void accumulate(const CycleAccumulator& other) {
        oprf_receiver_creation_cycles += other.oprf_receiver_creation_cycles;
        oprf_request_creation_cycles += other.oprf_request_creation_cycles;
        send_oprf_request_cycles += other.send_oprf_request_cycles;
        receive_oprf_request_cycles += other.receive_oprf_request_cycles;
        run_oprf_cycles += other.run_oprf_cycles;
        receive_oprf_response_cycles += other.receive_oprf_response_cycles;
        extract_hashes_cycles += other.extract_hashes_cycles;
        create_query_cycles += other.create_query_cycles;
        send_query_cycles += other.send_query_cycles;
        receive_query_cycles += other.receive_query_cycles;
        run_query_cycles += other.run_query_cycles;
        receive_query_response_cycles += other.receive_query_response_cycles;
        process_result_cycles += other.process_result_cycles;
        run_count += other.run_count;
        hash_oprf_request_cycles += other.hash_oprf_request_cycles;
        hash_oprf_response_cycles += other.hash_oprf_response_cycles;
        hash_received_query_cycles += other.hash_received_query_cycles;
        hash_query_response_cycles += other.hash_query_response_cycles;
        hash_final_result_cycles += other.hash_final_result_cycles;
    }

    // Method to print average cycles
    void print_average() const {
        if (run_count == 0) {
            std::cout << "No runs to average.\n";
            return;
        }

        std::cout << "[AVERAGE CYCLES] Client OPRF Receiver Creation\t\t" 
                  << (oprf_receiver_creation_cycles / run_count) << " cycles" << std::endl;
        std::cout << "[AVERAGE CYCLES] Client OPRF Request Creation\t\t" 
                  << (oprf_request_creation_cycles / run_count) << " cycles" << std::endl;
        std::cout << "[AVERAGE CYCLES] Client Hash OPRF Request\t\t" 
                  << (hash_oprf_request_cycles / run_count) << " cycles" << std::endl;
        std::cout << "[AVERAGE CYCLES] Client Send OPRF Request\t\t" 
                  << (send_oprf_request_cycles / run_count) << " cycles" << std::endl;
        std::cout << "[AVERAGE CYCLES] Server Receive OPRF Request\t\t" 
                  << (receive_oprf_request_cycles / run_count) << " cycles" << std::endl;
        std::cout << "[AVERAGE CYCLES] Server Run OPRF\t\t\t" 
                  << (run_oprf_cycles / run_count) << " cycles" << std::endl;
        std::cout << "[AVERAGE CYCLES] Server Hash OPRF Response\t\t" 
                  << (hash_oprf_response_cycles / run_count) << " cycles" << std::endl;
        std::cout << "[AVERAGE CYCLES] Client Receive OPRF Response\t\t" 
                  << (receive_oprf_response_cycles / run_count) << " cycles" << std::endl;
        std::cout << "[AVERAGE CYCLES] Client Extract Hashes\t\t\t" 
                  << (extract_hashes_cycles / run_count) << " cycles" << std::endl;
        std::cout << "[AVERAGE CYCLES] Client Create Query\t\t\t" 
                  << (create_query_cycles / run_count) << " cycles" << std::endl;
        std::cout << "[AVERAGE CYCLES] Client Hash Query\t\t\t" 
                  << (hash_received_query_cycles / run_count) << " cycles" << std::endl;
        std::cout << "[AVERAGE CYCLES] Client Send Query\t\t\t" 
                  << (send_query_cycles / run_count) << " cycles" << std::endl;
        std::cout << "[AVERAGE CYCLES] Server Receive Query\t\t\t" 
                  << (receive_query_cycles / run_count) << " cycles" << std::endl;
        std::cout << "[AVERAGE CYCLES] Server Run Query\t\t\t" 
                  << (run_query_cycles / run_count) << " cycles" << std::endl;
        std::cout << "[AVERAGE CYCLES] Server Hash Query Response\t\t" 
                  << (hash_query_response_cycles / run_count) << " cycles" << std::endl;
        std::cout << "[AVERAGE CYCLES] Client Receive Query Response\t\t" 
                  << (receive_query_response_cycles / run_count) << " cycles" << std::endl;
        std::cout << "[AVERAGE CYCLES] Client Process Result Parts\t\t" 
                  << (process_result_cycles / run_count) << " cycles" << std::endl;
        std::cout << "[AVERAGE CYCLES] Hash Final Result\t\t\t" 
                  << (hash_final_result_cycles / run_count) << " cycles" << std::endl;
    }
};