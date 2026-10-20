// NOLINTBEGIN(misc-include-cleaner) - Google Benchmark and Darwin sysctl/qos
// headers are not in clang-tidy's built-in IWYU map
#include <benchmark/benchmark.h>

#if defined(__APPLE__)
#include <pthread/qos.h>
#include <sys/sysctl.h>
#endif

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "systems/bitmap_flip.hpp"

namespace {

constexpr std::uint64_t kDeterministicBenchmarkSeed = 0x5EED'B17A'F119'2026ULL;

[[nodiscard]] constexpr auto bytes_per_row(std::size_t width) noexcept
    -> std::size_t {
  return (width / 8U) + ((width % 8U != 0U) ? 1U : 0U);
}

[[nodiscard]] auto make_test_buffer(std::size_t byte_count)
    -> std::vector<std::uint8_t> {
  std::mt19937_64 rng{kDeterministicBenchmarkSeed};
  std::uniform_int_distribution<unsigned int> dist{0U, 255U};
  std::vector<std::uint8_t> buffer(byte_count);
  for (auto& byte : buffer) {
    byte = static_cast<std::uint8_t>(dist(rng));
  }
  return buffer;
}

void BM_FlipBitmap_Baseline(benchmark::State& state, std::size_t width,
                            std::size_t height) {
  const std::size_t row_bytes = bytes_per_row(width);
  const std::size_t total_bytes = row_bytes * height;
  auto buffer = make_test_buffer(total_bytes);
  std::span<std::uint8_t> span(buffer.data(), buffer.size());

  // Warmup and zero padding bits (involution setup)
  static_cast<void>(systems::flip_bitmap_unaligned(span, width, height));

  for (auto _ : state) {
    static_cast<void>(systems::flip_bitmap_unaligned(span, width, height));
    benchmark::DoNotOptimize(buffer.data());
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(total_bytes));
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(width * height));
}

void BM_FlipBitmap_Candidate(benchmark::State& state, std::size_t width,
                             std::size_t height) {
  const std::size_t row_bytes = bytes_per_row(width);
  const std::size_t total_bytes = row_bytes * height;
  auto buffer = make_test_buffer(total_bytes);
  std::span<std::uint8_t> span(buffer.data(), buffer.size());

  // Warmup and zero padding bits (involution setup)
  static_cast<void>(
      systems::flip_bitmap_unaligned_optimized(span, width, height));

  for (auto _ : state) {
    static_cast<void>(
        systems::flip_bitmap_unaligned_optimized(span, width, height));
    benchmark::DoNotOptimize(buffer.data());
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(total_bytes));
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(width * height));
}

#define REGISTER_FLIP_BENCHMARK(width, height)                                \
  BENCHMARK_CAPTURE(BM_FlipBitmap_Baseline, width##x##height, width, height)  \
      ->Name("BM_FlipBitmap_Baseline/" #width "x" #height);                   \
  BENCHMARK_CAPTURE(BM_FlipBitmap_Candidate, width##x##height, width, height) \
      ->Name("BM_FlipBitmap_Candidate/" #width "x" #height)

REGISTER_FLIP_BENCHMARK(7, 1);
REGISTER_FLIP_BENCHMARK(10, 100);
REGISTER_FLIP_BENCHMARK(17, 100);
REGISTER_FLIP_BENCHMARK(29, 29);
REGISTER_FLIP_BENCHMARK(31, 64);
REGISTER_FLIP_BENCHMARK(63, 64);
REGISTER_FLIP_BENCHMARK(65, 64);
REGISTER_FLIP_BENCHMARK(127, 32);
REGISTER_FLIP_BENCHMARK(255, 16);
REGISTER_FLIP_BENCHMARK(511, 8);
REGISTER_FLIP_BENCHMARK(1023, 4);
REGISTER_FLIP_BENCHMARK(1366, 768);
REGISTER_FLIP_BENCHMARK(3839, 2160);
REGISTER_FLIP_BENCHMARK(5400, 3600);
REGISTER_FLIP_BENCHMARK(7200, 5400);
REGISTER_FLIP_BENCHMARK(10800, 7200);

#undef REGISTER_FLIP_BENCHMARK

#if defined(__APPLE__)
auto sysctl_string(const char* name) -> std::string {
  std::array<char, 256> buffer{};
  std::size_t size = buffer.size();
  if (sysctlbyname(name, buffer.data(), &size, nullptr, 0) == 0 && size > 0U) {
    return std::string{buffer.data()};
  }
  return {};
}

auto sysctl_int64(const char* name) -> std::int64_t {
  std::int64_t value = 0;
  std::size_t size = sizeof(value);
  if (sysctlbyname(name, &value, &size, nullptr, 0) == 0) {
    return value;
  }
  return 0;
}

void configure_darwin_benchmark_environment() {
  // Pin the benchmark main thread to QOS_CLASS_USER_INTERACTIVE so the XNU
  // AMP scheduler binds execution to the Performance cluster (perflevel0) and
  // ramps the CLPC frequency controller immediately.
  static_cast<void>(
      pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0));

  // Spin for 150 ms so the Apple Silicon Closed-Loop Performance Controller
  // (CLPC) migrates the thread onto a P-core and locks the PLL at peak P-state
  // before the first benchmark repetition begins.
  const auto warmup_deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
  std::uint64_t accumulator = 1U;
  while (std::chrono::steady_clock::now() < warmup_deadline) {
    for (int i = 0; i < 4096; ++i) {
      accumulator = (accumulator * 6364136223846793005ULL) + 1ULL;
    }
    benchmark::DoNotOptimize(accumulator);
  }

  const std::string brand = sysctl_string("machdep.cpu.brand_string");
  if (!brand.empty()) {
    benchmark::AddCustomContext("apple_silicon_cpu_brand", brand);
  }
  benchmark::AddCustomContext("darwin_thread_qos",
                              "QOS_CLASS_USER_INTERACTIVE");

  const std::int64_t p_cores = sysctl_int64("hw.perflevel0.physicalcpu");
  const std::int64_t e_cores = sysctl_int64("hw.perflevel1.physicalcpu");
  if (p_cores > 0) {
    benchmark::AddCustomContext("apple_perflevel0_p_cores",
                                std::to_string(p_cores));
    benchmark::AddCustomContext(
        "apple_perflevel0_l1d_kib",
        std::to_string(sysctl_int64("hw.perflevel0.l1dcachesize") / 1024));
    benchmark::AddCustomContext(
        "apple_perflevel0_l1i_kib",
        std::to_string(sysctl_int64("hw.perflevel0.l1icachesize") / 1024));
    benchmark::AddCustomContext(
        "apple_perflevel0_l2_kib",
        std::to_string(sysctl_int64("hw.perflevel0.l2cachesize") / 1024));
  }
  if (e_cores > 0) {
    benchmark::AddCustomContext("apple_perflevel1_e_cores",
                                std::to_string(e_cores));
  }
  const std::int64_t cacheline = sysctl_int64("hw.cachelinesize");
  if (cacheline > 0) {
    benchmark::AddCustomContext("apple_cacheline_bytes",
                                std::to_string(cacheline));
  }
}
#endif

}  // namespace

auto main(int argc, char** argv) -> int {
#if defined(__APPLE__)
  configure_darwin_benchmark_environment();
#endif
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
// NOLINTEND(misc-include-cleaner)
