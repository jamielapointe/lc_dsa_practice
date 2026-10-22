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
#include <string>
#include <vector>

extern "C" {
#include "systems/bitmap_flip.h"
}

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

void BM_FlipBitmap_C_Baseline(benchmark::State& state, std::size_t width,
                              std::size_t height) {
  const std::size_t row_bytes = bytes_per_row(width);
  const std::size_t total_bytes = row_bytes * height;
  auto buffer = make_test_buffer(total_bytes);

  static_cast<void>(
      bitmap_flip_unaligned(buffer.data(), buffer.size(), width, height));

  for (auto _ : state) {
    static_cast<void>(
        bitmap_flip_unaligned(buffer.data(), buffer.size(), width, height));
    benchmark::DoNotOptimize(buffer.data());
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(total_bytes));
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(width * height));
}

void BM_FlipBitmap_C_Candidate(benchmark::State& state, std::size_t width,
                               std::size_t height) {
  const std::size_t row_bytes = bytes_per_row(width);
  const std::size_t total_bytes = row_bytes * height;
  auto buffer = make_test_buffer(total_bytes);

  static_cast<void>(bitmap_flip_unaligned_optimized(
      buffer.data(), buffer.size(), width, height));

  for (auto _ : state) {
    static_cast<void>(bitmap_flip_unaligned_optimized(
        buffer.data(), buffer.size(), width, height));
    benchmark::DoNotOptimize(buffer.data());
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(total_bytes));
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(width * height));
}

#define REGISTER_C_FLIP_BENCHMARK(width, height)                               \
  BENCHMARK_CAPTURE(BM_FlipBitmap_C_Baseline, width##x##height, width, height) \
      ->Name("BM_FlipBitmap_C_Baseline/" #width "x" #height);                  \
  BENCHMARK_CAPTURE(BM_FlipBitmap_C_Candidate, width##x##height, width,        \
                    height)                                                    \
      ->Name("BM_FlipBitmap_C_Candidate/" #width "x" #height)

REGISTER_C_FLIP_BENCHMARK(7, 1);
REGISTER_C_FLIP_BENCHMARK(10, 100);
REGISTER_C_FLIP_BENCHMARK(17, 100);
REGISTER_C_FLIP_BENCHMARK(29, 29);
REGISTER_C_FLIP_BENCHMARK(31, 64);
REGISTER_C_FLIP_BENCHMARK(63, 64);
REGISTER_C_FLIP_BENCHMARK(65, 64);
REGISTER_C_FLIP_BENCHMARK(127, 32);
REGISTER_C_FLIP_BENCHMARK(255, 16);
REGISTER_C_FLIP_BENCHMARK(511, 8);
REGISTER_C_FLIP_BENCHMARK(1023, 4);
REGISTER_C_FLIP_BENCHMARK(1366, 768);
REGISTER_C_FLIP_BENCHMARK(3839, 2160);
REGISTER_C_FLIP_BENCHMARK(5400, 3600);
REGISTER_C_FLIP_BENCHMARK(7200, 5400);
REGISTER_C_FLIP_BENCHMARK(10800, 7200);

#undef REGISTER_C_FLIP_BENCHMARK

}  // namespace

auto main(int argc, char** argv) -> int {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
// NOLINTEND(misc-include-cleaner)
