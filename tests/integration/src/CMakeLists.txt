# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.

target_sources(integration_tests
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/integration_tests_runner.cpp
        ${CMAKE_CURRENT_LIST_DIR}/test_utils.cpp
        ${CMAKE_CURRENT_LIST_DIR}/SHA256.cpp
)

target_include_directories(integration_tests PRIVATE ${CMAKE_CURRENT_LIST_DIR})