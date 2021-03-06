#include "utility.h"

#ifdef WIN32
#include <windows.h>
// <windows.h> uses macros to #define a ton of symbols, two of which (DELETE and GetMessage)
// interfere with our code. DELETE shows up in the base.pb.h header generated from
// api/envoy/api/core/base.proto. Since it's a generated header, we can't #undef DELETE at
// the top of that header to avoid the collision. Similarly, GetMessage shows up in generated
// protobuf code so we can't #undef the symbol there.
#undef DELETE
#undef GetMessage
#endif

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <stdexcept>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codec.h"

#include "common/api/api_impl.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/common/lock_guard.h"
#include "common/common/stack_array.h"
#include "common/common/thread_impl.h"
#include "common/common/utility.h"
#include "common/config/bootstrap_json.h"
#include "common/json/json_loader.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/stats/stats_options_impl.h"
#include "common/filesystem/directory.h"

#include "test/test_common/printers.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

using testing::GTEST_FLAG(random_seed);

namespace Envoy {

// The purpose of using the static seed here is to use --test_arg=--gtest_random_seed=[seed]
// to specify the seed of the problem to replay.
int32_t getSeed() {
  static const int32_t seed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count();
  return seed;
}

TestRandomGenerator::TestRandomGenerator()
    : seed_(GTEST_FLAG(random_seed) == 0 ? getSeed() : GTEST_FLAG(random_seed)), generator_(seed_) {
  std::cerr << "TestRandomGenerator running with seed " << seed_ << "\n";
}

uint64_t TestRandomGenerator::random() { return generator_(); }

bool TestUtility::headerMapEqualIgnoreOrder(const Http::HeaderMap& lhs,
                                            const Http::HeaderMap& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  struct State {
    const Http::HeaderMap& lhs;
    bool equal;
  };

  State state{lhs, true};
  rhs.iterate(
      [](const Http::HeaderEntry& header, void* context) -> Http::HeaderMap::Iterate {
        State* state = static_cast<State*>(context);
        const Http::HeaderEntry* entry =
            state->lhs.get(Http::LowerCaseString(std::string(header.key().c_str())));
        if (entry == nullptr || (entry->value() != header.value().c_str())) {
          state->equal = false;
          return Http::HeaderMap::Iterate::Break;
        }
        return Http::HeaderMap::Iterate::Continue;
      },
      &state);

  return state.equal;
}

bool TestUtility::buffersEqual(const Buffer::Instance& lhs, const Buffer::Instance& rhs) {
  if (lhs.length() != rhs.length()) {
    return false;
  }

  uint64_t lhs_num_slices = lhs.getRawSlices(nullptr, 0);
  uint64_t rhs_num_slices = rhs.getRawSlices(nullptr, 0);
  if (lhs_num_slices != rhs_num_slices) {
    return false;
  }

  STACK_ARRAY(lhs_slices, Buffer::RawSlice, lhs_num_slices);
  lhs.getRawSlices(lhs_slices.begin(), lhs_num_slices);
  STACK_ARRAY(rhs_slices, Buffer::RawSlice, rhs_num_slices);
  rhs.getRawSlices(rhs_slices.begin(), rhs_num_slices);
  for (size_t i = 0; i < lhs_num_slices; i++) {
    if (lhs_slices[i].len_ != rhs_slices[i].len_) {
      return false;
    }

    if (0 != memcmp(lhs_slices[i].mem_, rhs_slices[i].mem_, lhs_slices[i].len_)) {
      return false;
    }
  }

  return true;
}

void TestUtility::feedBufferWithRandomCharacters(Buffer::Instance& buffer, uint64_t n_char,
                                                 uint64_t seed) {
  const std::string sample = "Neque porro quisquam est qui dolorem ipsum..";
  std::mt19937 generate(seed);
  std::uniform_int_distribution<> distribute(1, sample.length() - 1);
  std::string str{};
  for (uint64_t n = 0; n < n_char; ++n) {
    str += sample.at(distribute(generate));
  }
  buffer.add(str);
}

Stats::CounterSharedPtr TestUtility::findCounter(Stats::Store& store, const std::string& name) {
  return findByName(store.counters(), name);
}

Stats::GaugeSharedPtr TestUtility::findGauge(Stats::Store& store, const std::string& name) {
  return findByName(store.gauges(), name);
}

std::list<Network::Address::InstanceConstSharedPtr>
TestUtility::makeDnsResponse(const std::list<std::string>& addresses) {
  std::list<Network::Address::InstanceConstSharedPtr> ret;
  for (const auto& address : addresses) {
    ret.emplace_back(Network::Utility::parseInternetAddress(address));
  }
  return ret;
}

std::vector<std::string> TestUtility::listFiles(const std::string& path, bool recursive) {
  std::vector<std::string> file_names;
  Filesystem::Directory directory(path);
  for (const Filesystem::DirectoryEntry& entry : directory) {
    std::string file_name = fmt::format("{}/{}", path, entry.name_);
    if (entry.type_ == Filesystem::FileType::Directory) {
      if (recursive && entry.name_ != "." && entry.name_ != "..") {
        std::vector<std::string> more_file_names = listFiles(file_name, recursive);
        file_names.insert(file_names.end(), more_file_names.begin(), more_file_names.end());
      }
    } else { // regular file
      file_names.push_back(file_name);
    }
  }
  return file_names;
}

envoy::config::bootstrap::v2::Bootstrap
TestUtility::parseBootstrapFromJson(const std::string& json_string) {
  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Stats::StatsOptionsImpl stats_options;
  Config::BootstrapJson::translateBootstrap(*json_object_ptr, bootstrap, stats_options);
  return bootstrap;
}

std::vector<std::string> TestUtility::split(const std::string& source, char split) {
  return TestUtility::split(source, std::string{split});
}

std::vector<std::string> TestUtility::split(const std::string& source, const std::string& split,
                                            bool keep_empty_string) {
  std::vector<std::string> ret;
  const auto tokens_sv = StringUtil::splitToken(source, split, keep_empty_string);
  std::transform(tokens_sv.begin(), tokens_sv.end(), std::back_inserter(ret),
                 [](absl::string_view sv) { return std::string(sv); });
  return ret;
}

void TestUtility::renameFile(const std::string& old_name, const std::string& new_name) {
#ifdef WIN32
  // use MoveFileEx, since ::rename will not overwrite an existing file. See
  // https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/rename-wrename?view=vs-2017
  const BOOL rc = ::MoveFileEx(old_name.c_str(), new_name.c_str(), MOVEFILE_REPLACE_EXISTING);
  ASSERT_NE(0, rc);
#else
  const int rc = ::rename(old_name.c_str(), new_name.c_str());
  ASSERT_EQ(0, rc);
#endif
};

void TestUtility::createDirectory(const std::string& name) {
#ifdef WIN32
  ::_mkdir(name.c_str());
#else
  ::mkdir(name.c_str(), S_IRWXU);
#endif
}

void TestUtility::createSymlink(const std::string& target, const std::string& link) {
#ifdef WIN32
  const DWORD attributes = ::GetFileAttributes(target.c_str());
  ASSERT_NE(attributes, INVALID_FILE_ATTRIBUTES);
  int flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
  if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
    flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
  }

  const BOOLEAN rc = ::CreateSymbolicLink(link.c_str(), target.c_str(), flags);
  ASSERT_NE(rc, 0);
#else
  const int rc = ::symlink(target.c_str(), link.c_str());
  ASSERT_EQ(rc, 0);
#endif
}

// static
absl::Time TestUtility::parseTime(const std::string& input, const std::string& input_format) {
  absl::Time time;
  std::string parse_error;
  EXPECT_TRUE(absl::ParseTime(input_format, input, &time, &parse_error))
      << " error \"" << parse_error << "\" from failing to parse timestamp \"" << input
      << "\" with format string \"" << input_format << "\"";
  return time;
}

// static
std::string TestUtility::formatTime(const absl::Time input, const std::string& output_format) {
  static const absl::TimeZone utc = absl::UTCTimeZone();
  return absl::FormatTime(output_format, input, utc);
}

// static
std::string TestUtility::formatTime(const SystemTime input, const std::string& output_format) {
  return TestUtility::formatTime(absl::FromChrono(input), output_format);
}

// static
std::string TestUtility::convertTime(const std::string& input, const std::string& input_format,
                                     const std::string& output_format) {
  return TestUtility::formatTime(TestUtility::parseTime(input, input_format), output_format);
}

void ConditionalInitializer::setReady() {
  Thread::LockGuard lock(mutex_);
  EXPECT_FALSE(ready_);
  ready_ = true;
  cv_.notifyAll();
}

void ConditionalInitializer::waitReady() {
  Thread::LockGuard lock(mutex_);
  if (ready_) {
    ready_ = false;
    return;
  }

  cv_.wait(mutex_);
  EXPECT_TRUE(ready_);
  ready_ = false;
}

void ConditionalInitializer::wait() {
  Thread::LockGuard lock(mutex_);
  while (!ready_) {
    cv_.wait(mutex_);
  }
}

ScopedFdCloser::ScopedFdCloser(int fd) : fd_(fd) {}
ScopedFdCloser::~ScopedFdCloser() { ::close(fd_); }

AtomicFileUpdater::AtomicFileUpdater(const std::string& filename)
    : link_(filename), new_link_(absl::StrCat(filename, ".new")),
      target1_(absl::StrCat(filename, ".target1")), target2_(absl::StrCat(filename, ".target2")),
      use_target1_(true) {
  unlink(link_.c_str());
  unlink(new_link_.c_str());
  unlink(target1_.c_str());
  unlink(target2_.c_str());
}

void AtomicFileUpdater::update(const std::string& contents) {
  const std::string target = use_target1_ ? target1_ : target2_;
  use_target1_ = !use_target1_;
  {
    std::ofstream file(target);
    file << contents;
  }
  TestUtility::createSymlink(target, new_link_);
  TestUtility::renameFile(new_link_, link_);
}

constexpr std::chrono::milliseconds TestUtility::DefaultTimeout;

namespace Http {

// Satisfy linker
const uint32_t Http2Settings::DEFAULT_HPACK_TABLE_SIZE;
const uint32_t Http2Settings::DEFAULT_MAX_CONCURRENT_STREAMS;
const uint32_t Http2Settings::DEFAULT_INITIAL_STREAM_WINDOW_SIZE;
const uint32_t Http2Settings::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE;
const uint32_t Http2Settings::MIN_INITIAL_STREAM_WINDOW_SIZE;

TestHeaderMapImpl::TestHeaderMapImpl() : HeaderMapImpl() {}

TestHeaderMapImpl::TestHeaderMapImpl(
    const std::initializer_list<std::pair<std::string, std::string>>& values)
    : HeaderMapImpl() {
  for (auto& value : values) {
    addCopy(value.first, value.second);
  }
}

TestHeaderMapImpl::TestHeaderMapImpl(const HeaderMap& rhs) : HeaderMapImpl(rhs) {}

TestHeaderMapImpl::TestHeaderMapImpl(const TestHeaderMapImpl& rhs)
    : TestHeaderMapImpl(static_cast<const HeaderMap&>(rhs)) {}

TestHeaderMapImpl& TestHeaderMapImpl::operator=(const TestHeaderMapImpl& rhs) {
  if (&rhs == this) {
    return *this;
  }

  clear();
  copyFrom(rhs);

  return *this;
}

void TestHeaderMapImpl::addCopy(const std::string& key, const std::string& value) {
  addCopy(LowerCaseString(key), value);
}

void TestHeaderMapImpl::remove(const std::string& key) { remove(LowerCaseString(key)); }

std::string TestHeaderMapImpl::get_(const std::string& key) { return get_(LowerCaseString(key)); }

std::string TestHeaderMapImpl::get_(const LowerCaseString& key) {
  const HeaderEntry* header = get(key);
  if (!header) {
    return EMPTY_STRING;
  } else {
    return header->value().c_str();
  }
}

bool TestHeaderMapImpl::has(const std::string& key) { return get(LowerCaseString(key)) != nullptr; }

bool TestHeaderMapImpl::has(const LowerCaseString& key) { return get(key) != nullptr; }

} // namespace Http

namespace Stats {

MockedTestAllocator::MockedTestAllocator(const StatsOptions& stats_options)
    : TestAllocator(stats_options) {
  ON_CALL(*this, alloc(_)).WillByDefault(Invoke([this](absl::string_view name) -> RawStatData* {
    return TestAllocator::alloc(name);
  }));

  ON_CALL(*this, free(_)).WillByDefault(Invoke([this](RawStatData& data) -> void {
    return TestAllocator::free(data);
  }));

  EXPECT_CALL(*this, alloc(absl::string_view("stats.overflow")));
}

MockedTestAllocator::~MockedTestAllocator() {}

} // namespace Stats

namespace Thread {

// TODO(sesmith177) Tests should get the ThreadFactory from the same location as the main code
ThreadFactory& threadFactoryForTest() {
#ifdef WIN32
  static ThreadFactoryImplWin32* thread_factory = new ThreadFactoryImplWin32();
#else
  static ThreadFactoryImplPosix* thread_factory = new ThreadFactoryImplPosix();
#endif
  return *thread_factory;
}

} // namespace Thread

namespace Api {

ApiPtr createApiForTest(Stats::Store& stat_store) {
  return std::make_unique<Impl>(std::chrono::milliseconds(1000), Thread::threadFactoryForTest(),
                                stat_store);
}

} // namespace Api

} // namespace Envoy
