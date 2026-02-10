#include <doctest/doctest.h>
#include <utils/utils.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>

using namespace utils;
namespace fs = std::filesystem;

fs::path my_unique_path()
{
    // Create a random 128-bit number
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;

    uint64_t high = dist(gen);
    uint64_t low  = dist(gen);

    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << high
        << std::setw(16) << std::setfill('0') << low;

    return fs::path(oss.str());
}

fs::path createTmpPath()
{
    const auto temp = fs::temp_directory_path() / my_unique_path();
    fs::create_directories(temp);

    return temp;
}

void removeTmpPath(const fs::path &temp)
{
    fs::remove_all(temp);
}

TEST_CASE("readJsonFile")
{
    const auto tmpDir = createTmpPath();

    auto goodJsonPath = tmpDir / "good.json";
    {
        std::ofstream ofs(goodJsonPath.string());
        ofs << R"({"key": "value"})";
    }

    auto badJsonPath = tmpDir / "bad.json";
    {
        std::ofstream ofs(badJsonPath.string());
        ofs << R"({'key': "value"})";
    }

    // Test reading good JSON file
    auto goodJson = readJsonFile(goodJsonPath.string());
    std::cout << goodJson.dump() << std::endl;
    std::string a;
    goodJson.at("key").get_to(a);
    std::cout << "key: " << a << std::endl;
    CHECK(goodJson.is_object());
    CHECK_FALSE(goodJson.empty());

    // Test reading bad JSON file
    auto badJson = readJsonFile(badJsonPath.string());
    CHECK(badJson.is_null());

    // Clean up
    removeTmpPath(tmpDir);
}

TEST_CASE("writeJsonFile")
{
    const auto tmpDir = createTmpPath();

    auto jsonPath = tmpDir / "output.json";
    nlohmann::json jsonObj = {{"key", "value"}};

    CHECK(writeJsonFile(jsonPath.string(), jsonObj) == true);

    auto readJson = readJsonFile(jsonPath.string());
    CHECK(readJson["key"] == "value");

    // Clean up
    removeTmpPath(tmpDir);
}
