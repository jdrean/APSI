// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// STD
#include <sstream>
#include <iomanip>
#include <iostream>

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

using namespace std;
using namespace apsi;
using namespace apsi::receiver;
using namespace apsi::sender;
using namespace apsi::network;
using namespace apsi::util;
using namespace apsi::oprf;
using namespace seal;

unsigned int aux;
uint64_t time_start;
uint64_t time_end;

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
            Log::SetConsoleDisabled(true);
            // Log::SetLogLevel(Log::Level::info);

            ThreadPoolMgr::SetThreadCount(1);

            // Initialize sender items
            vector<Item> sender_items;
            for (size_t i = 0; i < sender_size; i++) {
                sender_items.push_back({ i + 1, i + 1 });
            }

            // Initialize SenderDB and OPRF key
            auto sender_db = make_shared<SenderDB>(params, 0);
            auto oprf_key = sender_db->get_oprf_key();
            sender_db->set_data(sender_items);

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
                unsigned int aux;
                unsigned long long time_start, time_end;

                time_start = __rdtscp(&aux);
                oprf::OPRFReceiver oprf_receiver = Receiver::CreateOPRFReceiver(recv_items);
                time_end = __rdtscp(&aux);
                local_accumulator.oprf_receiver_creation_cycles += (time_end - time_start);

                // Instrument: OPRF Request Creation
                time_start = __rdtscp(&aux);
                Request oprf_request = Receiver::CreateOPRFRequest(oprf_receiver);
                time_end = __rdtscp(&aux);
                local_accumulator.oprf_request_creation_cycles += (time_end - time_start);

                // Instrument: Send OPRF Request
                time_start = __rdtscp(&aux);
                chl.send(move(oprf_request));
                time_end = __rdtscp(&aux);
                size_t bytes_sent = chl.bytes_sent();
                local_accumulator.send_oprf_request_cycles += (time_end - time_start);

                cout << "New run" << endl;

                // Instrument: Receive OPRF Request
                size_t bytes_received_old = chl.bytes_received();
                time_start = __rdtscp(&aux);
                OPRFRequest oprf_request2 =
                    to_oprf_request(chl.receive_operation(nullptr, SenderOperationType::sop_oprf));
                time_end = __rdtscp(&aux);
                local_accumulator.receive_oprf_request_cycles += (time_end - time_start);
                size_t bytes_received = chl.bytes_received();
                cout << "Bytes received: " << bytes_received - bytes_received_old << endl;
                bytes_received_old = bytes_received;

                // Instrument: Run OPRF
                time_start = __rdtscp(&aux);
                Sender::RunOPRF(oprf_request2, oprf_key, chl);
                time_end = __rdtscp(&aux);
                local_accumulator.run_oprf_cycles += (time_end - time_start); // Including RunOPRF

                // Instrument: Receive OPRF Response
                time_start = __rdtscp(&aux);
                OPRFResponse oprf_response = to_oprf_response(chl.receive_response());
                time_end = __rdtscp(&aux);
                local_accumulator.receive_oprf_response_cycles += (time_end - time_start);

                bytes_received = chl.bytes_received();
                cout << "Bytes received: " << bytes_received - bytes_received_old << endl;
                bytes_received_old = bytes_received;

                // Instrument: Extract Hashes
                vector<HashedItem> hashed_recv_items;
                vector<LabelKey> label_keys;
                time_start = __rdtscp(&aux);
                tie(hashed_recv_items, label_keys) =
                    Receiver::ExtractHashes(oprf_response, oprf_receiver);
                time_end = __rdtscp(&aux);
                local_accumulator.extract_hashes_cycles += (time_end - time_start);
                assert(hashed_recv_items.size() == recv_items.size());

                // Instrument: Create Query
                time_start = __rdtscp(&aux);
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
                time_end = __rdtscp(&aux);
                local_accumulator.create_query_cycles += (time_end - time_start);

                // Instrument: Send Query
                time_start = __rdtscp(&aux);
                chl.send(move(recv_query));
                time_end = __rdtscp(&aux);
                local_accumulator.send_query_cycles += (time_end - time_start);

                // Instrument: Receive Query
                time_start = __rdtscp(&aux);
                QueryRequest sender_query = to_query_request(chl.receive_operation(seal_context));
                Query query(move(sender_query), sender_db);
                time_end = __rdtscp(&aux);
                local_accumulator.receive_query_cycles += (time_end - time_start);
                assert(expected_compr_mode == query.compr_mode());

                bytes_received = chl.bytes_received();
                cout << "Bytes received: " << bytes_received - bytes_received_old << endl;
                bytes_received_old = bytes_received;

                // Instrument: Run Query
                time_start = __rdtscp(&aux);
                Sender::RunQuery(query, chl);
                time_end = __rdtscp(&aux);
                local_accumulator.run_query_cycles += (time_end - time_start);

                // Instrument: Receive Query Response
                time_start = __rdtscp(&aux);
                QueryResponse query_response = to_query_response(chl.receive_response());
                time_end = __rdtscp(&aux);
                local_accumulator.receive_query_response_cycles += (time_end - time_start);
                uint32_t package_count = query_response->package_count;

                bytes_received = chl.bytes_received();
                cout << "Bytes received: " << bytes_received - bytes_received_old << endl;
                bytes_received_old = bytes_received;

                // Instrument: Process Result Parts
                vector<ResultPart> rps;
                time_start = __rdtscp(&aux);
                while (package_count--) {
                    rps.push_back(chl.receive_result(receiver.get_seal_context()));
                }
                auto query_result = receiver.process_result(label_keys, itt, rps);
                time_end = __rdtscp(&aux);
                local_accumulator.process_result_cycles += (time_end - time_start);

                bytes_received = chl.bytes_received();
                cout << "Bytes received: " << bytes_received - bytes_received_old << endl;
                bytes_received_old = bytes_received;

                // Verify results
                verify_unlabeled_results(query_result, recv_items, recv_int_items);

                // Increment run count
                // local_accumulator.run_count += 1i;
            }

            local_accumulator.run_count += 1;
            
            // Accumulate local cycles into global accumulator
            global_accumulator.accumulate(local_accumulator);

            // After all runs, print the average times
            local_accumulator.print_average();
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
	cout << "[TIME] " << func_name << "\t" << setprecision(6) << right
	    << setw(10) << elapsed_time / float(3600) << " us" << endl;
}

size_t NUM_REPEATS = 1;

int main(int argc, char **argv)
{
    unsigned long long elapsed_time = 0;
    PSIParams params = APSITests::create_params1();
    for (size_t i = 0; i < NUM_REPEATS; i++) {
        size_t sender_size = 4000;
        time_start = __rdtscp(&aux);
        APSITests::RunUnlabeledTest(
            sender_size,
            { { 0, 0 },
              { 1, 0 },
              { 500, 10 },
              { 500, 50 },
              { 500, 500 },
              { 1000, 0 },
              { 1000, 1 },
              { 1000, 500 },
              { 1000, 999 },
              { 1000, 1000 } },
            params,
            1);
        time_end = __rdtscp(&aux);
        elapsed_time += time_end - time_start;
    }
    elapsed_time /= NUM_REPEATS;
    print_time("RunUnlabeledTest", elapsed_time);

    // After all runs, print the average times
    APSITests::global_accumulator.print_average();
}
