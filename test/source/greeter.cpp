#include <doctest/doctest.h>
#include <scorbit_sdk/greeter.h>
#include <scorbit_sdk/version.h>

#include <string>

TEST_CASE("Greeter") {
  using namespace greeter;

  Greeter greeter("Tests");

  CHECK(greeter.greet(LanguageCode::EN) == "Hello, Tests!");
  CHECK(greeter.greet(LanguageCode::DE) == "Hallo Tests!");
  CHECK(greeter.greet(LanguageCode::ES) == "¡Hola Tests!");
  CHECK(greeter.greet(LanguageCode::FR) == "Bonjour Tests!");
}

TEST_CASE("Greeter version") {
  static_assert(std::string_view(SCORBIT_SDK_VERSION) == std::string_view("0.1.0"));
  CHECK(std::string(SCORBIT_SDK_VERSION) == std::string("0.1.0"));
}
