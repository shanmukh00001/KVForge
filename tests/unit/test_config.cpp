#include <gtest/gtest.h>
#include "config/config.hpp"

using namespace kvforge;

// Test 1: Check that default values are set correctly when no arguments are passed
TEST(ConfigTest, Defaults) {
    Config cfg;
    std::string err;
    char* argv[] = {(char*)"kvforge"};
    
    ConfigStatus status = parse_args(1, argv, cfg, err);
    
    EXPECT_EQ(status, ConfigStatus::OK);
    EXPECT_EQ(cfg.bind_address, "127.0.0.1");
    EXPECT_EQ(cfg.port, 6380);
    EXPECT_EQ(cfg.max_clients, 10000u);
    EXPECT_EQ(cfg.log_level, LogLevel::INFO);
    EXPECT_FALSE(cfg.appendonly);
    EXPECT_EQ(cfg.appendfsync, "everysec");
}

// Test 2: Check that custom valid arguments are parsed correctly
TEST(ConfigTest, CustomValidArgs) {
    Config cfg;
    std::string err;
    char* argv[] = {
        (char*)"kvforge",
        (char*)"--bind", (char*)"0.0.0.0",
        (char*)"--port", (char*)"7000",
        (char*)"--max-clients", (char*)"500",
        (char*)"--loglevel", (char*)"debug",
        (char*)"--appendonly", (char*)"yes",
        (char*)"--appendfsync", (char*)"always"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    ConfigStatus status = parse_args(argc, argv, cfg, err);

    EXPECT_EQ(status, ConfigStatus::OK);
    EXPECT_EQ(cfg.bind_address, "0.0.0.0");
    EXPECT_EQ(cfg.port, 7000);
    EXPECT_EQ(cfg.max_clients, 500u);
    EXPECT_EQ(cfg.log_level, LogLevel::DEBUG);
    EXPECT_TRUE(cfg.appendonly);
    EXPECT_EQ(cfg.appendfsync, "always");
}

// Test 3: Check that invalid ports are rejected
TEST(ConfigTest, InvalidPort) {
    Config cfg;
    std::string err;
    
    char* argv_high[] = {(char*)"kvforge", (char*)"--port", (char*)"70000"};
    EXPECT_EQ(parse_args(3, argv_high, cfg, err), ConfigStatus::ERROR);
    EXPECT_FALSE(err.empty());

    char* argv_text[] = {(char*)"kvforge", (char*)"--port", (char*)"not_a_number"};
    EXPECT_EQ(parse_args(3, argv_text, cfg, err), ConfigStatus::ERROR);
}

// Test 4: Check that invalid loglevel is rejected
TEST(ConfigTest, InvalidLogLevel) {
    Config cfg;
    std::string err;
    char* argv[] = {(char*)"kvforge", (char*)"--loglevel", (char*)"superverbose"};
    
    EXPECT_EQ(parse_args(3, argv, cfg, err), ConfigStatus::ERROR);
    EXPECT_FALSE(err.empty());
}

// Test 5: Check that --help and --version return proper exit statuses
TEST(ConfigTest, HelpAndVersion) {
    Config cfg;
    std::string err;

    char* argv_help[] = {(char*)"kvforge", (char*)"--help"};
    EXPECT_EQ(parse_args(2, argv_help, cfg, err), ConfigStatus::EXIT_HELP);

    char* argv_ver[] = {(char*)"kvforge", (char*)"--version"};
    EXPECT_EQ(parse_args(2, argv_ver, cfg, err), ConfigStatus::EXIT_VER);
}