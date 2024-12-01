// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// STD
#include <sstream>
#include <iomanip>
#include <iostream>
#include <chrono>

// APSI
#include "apsi/log.h"
#include "apsi/network/stream_channel.h"
#include "apsi/oprf/oprf_sender.h"
#include "apsi/receiver.h"
#include "apsi/sender.h"
#include "apsi/sender_db.h"
#include "apsi/thread_pool_mgr.h"
#include "test_utils.h"

#include <x86intrin.h>
#include <SHA256.h>

using namespace std;
using namespace std::chrono;
using namespace apsi;
using namespace apsi::receiver;
using namespace apsi::sender;
using namespace apsi::network;
using namespace apsi::util;
using namespace apsi::oprf;
using namespace seal;

class Timer {
public:
    Timer() {
        print_timestamp("Program started at: ");
    }
    
    ~Timer() {
        print_timestamp("Program ended at: ");
    }

private:
    static void print_timestamp(const char* prefix) {
        using namespace std::chrono;
        auto now = high_resolution_clock::now();
        auto duration = now.time_since_epoch();
        
        // Get seconds
        auto secs = duration_cast<std::chrono::seconds>(duration);
        
        // Get just the nanoseconds part by taking remainder after seconds
        auto nsecs = duration_cast<std::chrono::nanoseconds>(duration - secs).count() % 1000000000;
        
        // Print with exact same format as date +%s.%N
        std::cout << prefix 
                 << secs.count() << "."
                 << std::setfill('0') << std::setw(9) << nsecs
                 << std::endl;
    }
};

// Global timer instance
static Timer global_timer;

unsigned int aux;
uint64_t cycle_start;
uint64_t cycle_end;

namespace APSITests {
    namespace {
        // Global Cycle Accumulator
        CycleAccumulator global_accumulator;

        void RunUnlabeledTest(
            size_t sender_size,
            vector<pair<size_t, size_t>> client_total_and_int_sizes,
            const PSIParams &params,
            size_t num_threads,
            bool use_different_compression = false)
        {
            SHA256 sha;

            unsigned int aux;
            unsigned long long cycle_start, cycle_end;

            Log::SetConsoleDisabled(true);
            // Log::SetLogLevel(Log::Level::info);

            ThreadPoolMgr::SetThreadCount(1);

            // Initialize sender items
            vector<Item> sender_items;
            for (size_t i = 0; i < sender_size; i++) {
                sender_items.push_back({ i + 1, i + 1 });
            }

            // Hash and count bytes of sender_items
            size_t total_bytes = sender_items.size() * sizeof(Item);
            string sender_items_bytes;
            sender_items_bytes.reserve(total_bytes);
            for (const auto& item : sender_items) {
                sender_items_bytes.append(reinterpret_cast<const char*>(&item), sizeof(Item));
            }
            cycle_start = __rdtscp(&aux);
            sha.update(sender_items_bytes);
            auto digest = sha.digest();
            string hash = SHA256::toString(digest);
            cycle_end = __rdtscp(&aux);
            cout << "[INFO] Sender items total bytes: " << total_bytes << endl;
            cout << "[INFO] Hash size: " << digest.size() << " bytes" << endl;
            //cout << "[INFO] Hash (hex): " << hash << endl;
            cout << "[CYCLES] Hash sender items\t\t\t" 
                 << (cycle_end - cycle_start) << " cycles" << endl;
            cout << "[TIME] Hash sender items\t\t\t" 
                 << int((cycle_end - cycle_start) / 3600) << " us" << endl;

            // Initialize SenderDB and OPRF key
            cycle_start = __rdtscp(&aux);
            auto sender_db = make_shared<SenderDB>(params, 0);
            auto oprf_key = sender_db->get_oprf_key();
            sender_db->set_data(sender_items);
            cycle_end = __rdtscp(&aux);
            auto server_init_cycles = (cycle_end - cycle_start);

            cout << "[CYCLES] Server Init\t\t\t" 
                 << server_init_cycles << " cycles" << endl;
            cout << "[TIME] Server Init\t\t\t" 
                 << int(server_init_cycles / 3600) << " us" << endl;

            // Initialize SEAL context
            auto seal_context = sender_db->get_seal_context();

            // Initialize StreamChannel
            stringstream ss;
            StreamChannel chl(ss);

            // Initialize Receiver
            Receiver receiver(params);

            // Local Cycle Accumulator for this Run
            CycleAccumulator local_accumulator;

            for (auto client_total_and_int_size : client_total_and_int_sizes) {
                auto client_size = client_total_and_int_size.first;
                auto int_size = client_total_and_int_size.second;
                assert(int_size <= client_size);

                cout << "Client size: " << client_size << endl;
                cout << "Int size: " << int_size << endl;

                // Prepare receiving items
                vector<Item> recv_int_items = rand_subset(sender_items, int_size);
                vector<Item> recv_items;
                for (auto item : recv_int_items) {
                    recv_items.push_back(item);
                }
                for (size_t i = int_size; i < client_size; i++) {
                    recv_items.push_back({ i + 1, ~(i + 1) });
                }

                // Instrument: OPRF Receiver Creation
                cycle_start = __rdtscp(&aux);
                oprf::OPRFReceiver oprf_receiver = Receiver::CreateOPRFReceiver(recv_items);
                cycle_end = __rdtscp(&aux);
                local_accumulator.oprf_receiver_creation_cycles += (cycle_end - cycle_start);

                // Instrument: OPRF Request Creation
                cycle_start = __rdtscp(&aux);
                Request oprf_request = Receiver::CreateOPRFRequest(oprf_receiver);
                cycle_end = __rdtscp(&aux);
                local_accumulator.oprf_request_creation_cycles += (cycle_end - cycle_start);

                // Instrument: Send OPRF Request
                size_t bytes_sent_old = chl.bytes_sent();
                cycle_start = __rdtscp(&aux);
                chl.send(move(oprf_request));
                cycle_end = __rdtscp(&aux);
                local_accumulator.send_oprf_request_cycles += (cycle_end - cycle_start);

                size_t bytes_sent = chl.bytes_sent();
                cout << "OPRF Request Bytes sent: " << bytes_sent - bytes_sent_old << endl;
                // cout << "SS String len: " << ss.str().substr(bytes_sent_old, bytes_sent - bytes_sent_old).length() << endl;
                cycle_start = __rdtscp(&aux);
                sha.update(ss.str().substr(bytes_sent_old, bytes_sent - bytes_sent_old));
                cycle_end = __rdtscp(&aux);
                local_accumulator.hash_oprf_request_cycles += (cycle_end - cycle_start);
                bytes_sent_old = bytes_sent;

                cout << "New run" << endl;

                // Instrument: Receive OPRF Request
                cycle_start = __rdtscp(&aux);
                OPRFRequest oprf_request2 =
                    to_oprf_request(chl.receive_operation(nullptr, SenderOperationType::sop_oprf));
                cycle_end = __rdtscp(&aux);
                local_accumulator.receive_oprf_request_cycles += (cycle_end - cycle_start);

                // Instrument: Run OPRF
                cycle_start = __rdtscp(&aux);
                Sender::RunOPRF(oprf_request2, oprf_key, chl);
                cycle_end = __rdtscp(&aux);
                local_accumulator.run_oprf_cycles += (cycle_end - cycle_start); // Including RunOPRF

                bytes_sent = chl.bytes_sent();
                cout << "OPRF Response Bytes sent: " << bytes_sent - bytes_sent_old << endl;
                // cout << "SS String len: " << ss.str().substr(bytes_sent_old, bytes_sent - bytes_sent_old).length() << endl;
                cycle_start = __rdtscp(&aux);
                sha.update(ss.str().substr(bytes_sent_old, bytes_sent - bytes_sent_old));
                cycle_end = __rdtscp(&aux);
                local_accumulator.hash_oprf_response_cycles += (cycle_end - cycle_start);
                bytes_sent_old = bytes_sent;

                // Instrument: Receive OPRF Response
                cycle_start = __rdtscp(&aux);
                OPRFResponse oprf_response = to_oprf_response(chl.receive_response());
                cycle_end = __rdtscp(&aux);
                local_accumulator.receive_oprf_response_cycles += (cycle_end - cycle_start);

                // Instrument: Extract Hashes
                vector<HashedItem> hashed_recv_items;
                vector<LabelKey> label_keys;
                cycle_start = __rdtscp(&aux);
                tie(hashed_recv_items, label_keys) =
                    Receiver::ExtractHashes(oprf_response, oprf_receiver);
                cycle_end = __rdtscp(&aux);
                local_accumulator.extract_hashes_cycles += (cycle_end - cycle_start);
                assert(hashed_recv_items.size() == recv_items.size());

                // Instrument: Create Query
                cycle_start = __rdtscp(&aux);
                pair<Request, IndexTranslationTable> recv_query_pair =
                    receiver.create_query(hashed_recv_items);
                QueryRequest recv_query = to_query_request(move(recv_query_pair.first));
                compr_mode_type expected_compr_mode = recv_query->compr_mode;

                if (use_different_compression &&
                    Serialization::IsSupportedComprMode(compr_mode_type::zlib) &&
                    Serialization::IsSupportedComprMode(compr_mode_type::zstd)) {
                    if (recv_query->compr_mode == compr_mode_type::zstd) {
                        recv_query->compr_mode = compr_mode_type::zlib;
                        expected_compr_mode = compr_mode_type::zlib;
                    } else {
                        recv_query->compr_mode = compr_mode_type::zstd;
                        expected_compr_mode = compr_mode_type::zstd;
                    }
                }

                IndexTranslationTable itt = move(recv_query_pair.second);
                cycle_end = __rdtscp(&aux);
                local_accumulator.create_query_cycles += (cycle_end - cycle_start);

                // Instrument: Send Query
                cycle_start = __rdtscp(&aux);
                chl.send(move(recv_query));
                cycle_end = __rdtscp(&aux);
                local_accumulator.send_query_cycles += (cycle_end - cycle_start);

                bytes_sent = chl.bytes_sent();
                cout << "PSI Query Bytes sent: " << bytes_sent - bytes_sent_old << endl;
                // cout << "SS String len: " << ss.str().substr(bytes_sent_old, bytes_sent - bytes_sent_old).length() << endl;
                cycle_start = __rdtscp(&aux);
                sha.update(ss.str().substr(bytes_sent_old, bytes_sent - bytes_sent_old));
                cycle_end = __rdtscp(&aux);
                local_accumulator.hash_received_query_cycles += (cycle_end - cycle_start);
                bytes_sent_old = bytes_sent;

                // Instrument: Receive Query
                cycle_start = __rdtscp(&aux);
                QueryRequest sender_query = to_query_request(chl.receive_operation(seal_context));
                Query query(move(sender_query), sender_db);
                cycle_end = __rdtscp(&aux);
                local_accumulator.receive_query_cycles += (cycle_end - cycle_start);
                assert(expected_compr_mode == query.compr_mode());

                // Instrument: Run Query
                cycle_start = __rdtscp(&aux);
                Sender::RunQuery(query, chl);
                cycle_end = __rdtscp(&aux);
                local_accumulator.run_query_cycles += (cycle_end - cycle_start);

                bytes_sent = chl.bytes_sent();
                cout << "PSI Response Bytes sent: " << bytes_sent - bytes_sent_old << endl;
                // cout << "SS String len: " << ss.str().substr(bytes_sent_old, bytes_sent - bytes_sent_old).length() << endl;
                cycle_start = __rdtscp(&aux);
                sha.update(ss.str().substr(bytes_sent_old, bytes_sent - bytes_sent_old));
                cycle_end = __rdtscp(&aux);
                local_accumulator.hash_query_response_cycles += (cycle_end - cycle_start);
                bytes_sent_old = bytes_sent;

                // Instrument: Receive Query Response
                cycle_start = __rdtscp(&aux);
                QueryResponse query_response = to_query_response(chl.receive_response());
                cycle_end = __rdtscp(&aux);
                local_accumulator.receive_query_response_cycles += (cycle_end - cycle_start);
                uint32_t package_count = query_response->package_count;

                // Instrument: Process Result Parts
                vector<ResultPart> rps;
                cycle_start = __rdtscp(&aux);
                while (package_count--) {
                    rps.push_back(chl.receive_result(receiver.get_seal_context()));
                }
                auto query_result = receiver.process_result(label_keys, itt, rps);
                cycle_end = __rdtscp(&aux);
                local_accumulator.process_result_cycles += (cycle_end - cycle_start);

                // Verify results
                verify_unlabeled_results(query_result, recv_items, recv_int_items);

                // Increment run count
                // local_accumulator.run_count += 1i;
            }

            local_accumulator.run_count += 1;
            
            // Accumulate local cycles into global accumulator
            global_accumulator.accumulate(local_accumulator);

            // After all runs, print the average times
            //local_accumulator.print_average();
            cout << endl;
        }

        // void RunLabeledTest(
        //     size_t sender_size,
        //     vector<pair<size_t, size_t>> client_total_and_int_sizes,
        //     const PSIParams &params,
        //     size_t num_threads)
        // {
        //     Log::SetConsoleDisabled(true);
        //     Log::SetLogLevel(Log::Level::info);

        //     ThreadPoolMgr::SetThreadCount(num_threads);

        //     vector<pair<Item, Label>> sender_items;
        //     for (size_t i = 0; i < sender_size; i++) {
        //         sender_items.push_back(make_pair(
        //             Item(i + 1, i + 1),
        //             create_label(seal::util::safe_cast<unsigned char>((i + 1) & 0xFF), 10)));
        //     }

        //     auto sender_db = make_shared<SenderDB>(params, 10, 4, true);
        //     sender_db->set_data(sender_items);
        //     auto oprf_key = sender_db->get_oprf_key();

        //     auto seal_context = sender_db->get_seal_context();

        //     stringstream ss;
        //     StreamChannel chl(ss);

        //     Receiver receiver(params);

        //     for (auto client_total_and_int_size : client_total_and_int_sizes) {
        //         auto client_size = client_total_and_int_size.first;
        //         auto int_size = client_total_and_int_size.second;
        //         assert(int_size <= client_size);

        //         vector<Item> recv_int_items = rand_subset(sender_items, int_size);
        //         vector<Item> recv_items;
        //         for (auto item : recv_int_items) {
        //             recv_items.push_back(item);
        //         }
        //         for (size_t i = int_size; i < client_size; i++) {
        //             recv_items.push_back({ i + 1, ~(i + 1) });
        //         }

        //         // Create the OPRF receiver
        //         oprf::OPRFReceiver oprf_receiver = Receiver::CreateOPRFReceiver(recv_items);
        //         Request oprf_request = Receiver::CreateOPRFRequest(oprf_receiver);

        //         // Send the OPRF request
        //         chl.send(move(oprf_request));
        //         size_t bytes_sent = chl.bytes_sent();

        //         // Receive the OPRF request and process response
        //         OPRFRequest oprf_request2 =
        //             to_oprf_request(chl.receive_operation(nullptr, SenderOperationType::sop_oprf));
        //         size_t bytes_received = chl.bytes_received();
        //         assert(bytes_sent == bytes_received);
        //         Sender::RunOPRF(oprf_request2, oprf_key, chl);

        //         // Receive OPRF response
        //         OPRFResponse oprf_response = to_oprf_response(chl.receive_response());
        //         vector<HashedItem> hashed_recv_items;
        //         vector<LabelKey> label_keys;
        //         tie(hashed_recv_items, label_keys) =
        //             Receiver::ExtractHashes(oprf_response, oprf_receiver);
        //         assert(hashed_recv_items.size() == recv_items.size());

        //         // Create query and send
        //         pair<Request, IndexTranslationTable> recv_query =
        //             receiver.create_query(hashed_recv_items);
        //         IndexTranslationTable itt = move(recv_query.second);
        //         chl.send(move(recv_query.first));

        //         // Receive the query and process response
        //         QueryRequest sender_query = to_query_request(chl.receive_operation(seal_context));
        //         Query query(move(sender_query), sender_db);
        //         Sender::RunQuery(query, chl);

        //         // Receive query response
        //         QueryResponse query_response = to_query_response(chl.receive_response());
        //         uint32_t package_count = query_response->package_count;

        //         // Receive all result parts and process result
        //         vector<ResultPart> rps;
        //         while (package_count--) {
        //             rps.push_back(chl.receive_result(receiver.get_seal_context()));
        //         }
        //         auto query_result = receiver.process_result(label_keys, itt, rps);

        //         verify_labeled_results(query_result, recv_items, recv_int_items, sender_items);
        //     }
        // }
    } // namespace
} // namespace APSITests

void print_time(string func_name, unsigned long long elapsed_time) {
	cout << "[CYCLES] " << func_name << "\t" << setprecision(6) << right
	    << setw(10) << elapsed_time << " cycles" << endl;
}

size_t NUM_REPEATS = 1;

int main(int argc, char **argv)
{
    cout << "Starting APSI tests" << endl;

    unsigned long long elapsed_time = 0;
    PSIParams params = APSITests::create_params1();
    for (size_t i = 0; i < NUM_REPEATS; i++) {
        size_t sender_size = 1<<20;
        cycle_start = __rdtscp(&aux);
        APSITests::RunUnlabeledTest(
            sender_size,
            { { 3000, 1000 } },
            params,
            1);
        cycle_end = __rdtscp(&aux);
        elapsed_time += cycle_end - cycle_start;
    }
    elapsed_time /= NUM_REPEATS;
    print_time("RunUnlabeledTest", elapsed_time);

    // After all runs, print the average times
    APSITests::global_accumulator.print_average();
}
