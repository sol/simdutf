#include "simdutf.h"

#include <array>
#include <random>
#include <list>
#include <algorithm>
#include <stdexcept>

#include "reference/encode_utf16.h"
#include "test_macros.h"

namespace utf16::random {

  /*
    Generates valid random UTF-16

    It might generate streams consisting:
    - only single 16-bit words (Generator(..., 1, 0));
    - only surrogate pairs, two 16-bit words (Generator(..., 0, 1))
    - mixed, depending on given probabilities (Generator(..., 1, 1))
  */
  class Generator {
    std::mt19937 gen;

  public:
    Generator(std::random_device& rd, int single_word_prob, int two_words_probability)
      : gen{rd()}
      , utf16_length({double(single_word_prob),
                      double(single_word_prob),
                      double(2 * two_words_probability)}) {}

    std::vector<uint16_t> generate(size_t size);
    std::vector<uint16_t> generate(size_t size, long seed);

  private:
    std::discrete_distribution<> utf16_length;
    std::uniform_int_distribution<uint32_t> single_word0{0x0000'0000, 0x0000'd7ff};
    std::uniform_int_distribution<uint32_t> single_word1{0x0000'e000, 0x0000'ffff};
    std::uniform_int_distribution<uint32_t> two_words   {0x0001'0000, 0x0010'ffff};
    uint32_t generate();
  };

  std::vector<uint16_t> Generator::generate(size_t size)
  {
    if (size % 2 == 1)
      throw std::invalid_argument("Not implemented yet");

    std::vector<uint16_t> result;
    result.reserve(size);

    uint16_t W1;
    uint16_t W2;
    while (result.size() < size) {
      const uint32_t value = generate();
      switch (simdutf::tests::reference::utf16::encode(value, W1, W2)) {
        case 0:
          throw std::runtime_error("Random UTF-16 generator is broken");
        case 1:
          result.push_back(W1);
          break;
        case 2:
          result.push_back(W1);
          result.push_back(W2);
          break;
      }
    }

    return result;
  }

  std::vector<uint16_t> Generator::generate(size_t size, long seed) {
    gen.seed(seed);
    return generate(size);
  }

  uint32_t Generator::generate() {
    switch (utf16_length(gen)) {
      case 0:
        return single_word0(gen);
      case 1:
        return single_word1(gen);
      case 2:
        return two_words(gen);
      default:
        abort();
    }
  }

} // namespace utf16::random


std::vector<uint16_t> generate_valid_utf16(size_t size = 512) {
  std::random_device rd{};
  utf16::random::Generator generator{rd, 1, 0};
  return generator.generate(size);
}

TEST(validate_utf16__returns_true_for_valid_input__single_words) {
  std::random_device rd{};
  utf16::random::Generator generator{rd, 1, 0};
  const auto utf16{generator.generate(512)};

  ASSERT_TRUE(implementation.validate_utf16(
                reinterpret_cast<const char16_t*>(utf16.data()), utf16.size() * 2));
}

TEST(validate_utf16__returns_true_for_valid_input__surrogate_pairs) {
  std::random_device rd{};
  utf16::random::Generator generator{rd, 0, 1};
  const auto utf16{generator.generate(512)};

  ASSERT_TRUE(implementation.validate_utf16(
                reinterpret_cast<const char16_t*>(utf16.data()), utf16.size() * 2));
}

// mixed = either 16-bit or 32-bit codewords
TEST(validate_utf16__returns_true_for_valid_input__mixed) {
  std::random_device rd{};
  utf16::random::Generator generator{rd, 1, 1};
  const auto utf16{generator.generate(512)};

  ASSERT_TRUE(implementation.validate_utf16(
                reinterpret_cast<const char16_t*>(utf16.data()), utf16.size() * 2));
}

TEST(validate_utf16__returns_true_for_empty_string) {
  const char16_t* buf = (char16_t*)"";

  ASSERT_TRUE(implementation.validate_utf16(buf, 0));
}

TEST(validate_utf16__returns_false_when_input_has_odd_number_of_bytes) {
  const char16_t* buf = (char16_t*)"?";

  ASSERT_FALSE(implementation.validate_utf16(buf, 1));
}

// The first word must not be in range [0xDC00 .. 0xDFFF]
/*
2.2 Decoding UTF-16

   [...]

   1) If W1 < 0xD800 or W1 > 0xDFFF, the character value U is the value
      of W1. Terminate.

   2) Determine if W1 is between 0xD800 and 0xDBFF. If not, the sequence
      is in error [...]
*/
TEST(validate_utf16__returns_false_when_input_has_wrong_first_word_value) {
  auto utf16{generate_valid_utf16(128)};
  const char16_t*  buf = reinterpret_cast<const char16_t*>(utf16.data());
  const size_t len = 2 * utf16.size();

  for (uint16_t wrong_value = 0xdc00; wrong_value <= 0xdfff; wrong_value++) {
    for (size_t i=0; i < utf16.size(); i++) {
      const uint16_t old = utf16[i];
      utf16[i] = wrong_value;

      ASSERT_FALSE(implementation.validate_utf16(buf, len));

      utf16[i] = old;
    }
  }
}

/*
 RFC-2781:

 3) [..] if W2 is not between 0xDC00 and 0xDFFF, the sequence is in error.
    Terminate.
*/
TEST(validate_utf16__returns_false_when_input_has_wrong_second_word_value) {
  auto utf16{generate_valid_utf16(128)};
  const char16_t*  buf = reinterpret_cast<const char16_t*>(utf16.data());
  const size_t len = 2 * utf16.size();

  const std::array<uint16_t, 5> sample_wrong_second_word{
    0x0000, 0x1000, 0xdbff, 0xe000, 0xffff
  };

  const uint16_t valid_surrogate_W1 = 0xd800;
  for (uint16_t W2: sample_wrong_second_word) {
    for (size_t i=0; i < utf16.size() - 1; i++) {
      const uint16_t old_W1 = utf16[i + 0];
      const uint16_t old_W2 = utf16[i + 1];

      utf16[i + 0] = valid_surrogate_W1;
      utf16[i + 1] = W2;

      ASSERT_FALSE(implementation.validate_utf16(buf, len));

      utf16[i + 0] = old_W1;
      utf16[i + 1] = old_W2;
    }
  }
}

/*
 RFC-2781:

 3) If there is no W2 (that is, the sequence ends with W1) [...]
    the sequence is in error. Terminate.
*/
TEST(validate_utf16__returns_false_when_input_is_truncated) {
  const uint16_t valid_surrogate_W1 = 0xd800;
  for (size_t size = 1; size < 128; size++) {
    auto utf16{generate_valid_utf16(128)};
    const char16_t*  buf = reinterpret_cast<const char16_t*>(utf16.data());
    const size_t len = 2 * utf16.size();

    utf16[size - 1] = valid_surrogate_W1;

    ASSERT_FALSE(implementation.validate_utf16(buf, len));
  }
}

int main() {
  for (const auto& implementation: simdutf::available_implementations) {
    if (implementation == nullptr) {
      puts("SIMDUTF implementation is null");
      abort();
    }

    const simdutf::implementation& impl = *implementation;
    printf("Checking implementation %s\n", implementation->name().c_str());

    for (auto test: test_procedures())
      test(*implementation);
  }
}
