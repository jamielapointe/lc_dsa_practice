// NOLINTBEGIN(misc-include-cleaner) - Google Benchmark and Darwin sysctl/qos
// headers are not in clang-tidy's built-in IWYU map
#include <benchmark/benchmark.h>

#if defined(__APPLE__)
#include <pthread/qos.h>
#include <sys/sysctl.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>

extern "C" {
#include "systems/huffman_tree.h"
}

namespace {

constexpr std::uint64_t kDeterministicBenchmarkSeed = 0x5EED'B17A'F119'2026ULL;

enum class WorkloadKind : std::uint8_t {
  kUniform8BitShuffled = 0U,
  kDeflateSkewedShuffled = 1U,
  kDeepSpine32Shuffled = 2U,
  kReverseSorted8Bit = 3U,
};

[[nodiscard]] auto make_256_entries(const WorkloadKind kind)
    -> std::array<struct huffman_symbol_length, HUFFMAN_MAX_SYMBOLS> {
  std::array<struct huffman_symbol_length, HUFFMAN_MAX_SYMBOLS> entries{};

  for (std::size_t i = 0U; i < HUFFMAN_MAX_SYMBOLS; ++i) {
    entries[i].symbol = static_cast<std::uint8_t>(i);
    entries[i].length = 8U;
  }

  switch (kind) {
    case WorkloadKind::kUniform8BitShuffled: {
      std::mt19937_64 rng{kDeterministicBenchmarkSeed};
      std::shuffle(entries.begin(), entries.end(), rng);
      break;
    }
    case WorkloadKind::kDeflateSkewedShuffled: {
      // Full 256-symbol canonical Huffman tree with depths 5..10:
      // 4x len 5 + 12x len 6 + 32x len 7 + 48x len 8 + 96x len 9 + 64x len 10
      // Kraft sum = (4*32 + 12*16 + 32*8 + 48*4 + 96*2 + 64*1) / 1024 = 1.0
      std::size_t idx = 0U;
      constexpr std::array<std::pair<std::size_t, std::uint8_t>, 6> kBuckets = {
          {{4U, 5U}, {12U, 6U}, {32U, 7U}, {48U, 8U}, {96U, 9U}, {64U, 10U}}};
      for (const auto& [bucket_count, len] : kBuckets) {
        for (std::size_t k = 0U; k < bucket_count; ++k) {
          entries[idx].length = len;
          ++idx;
        }
      }
      std::mt19937_64 rng{kDeterministicBenchmarkSeed ^ 0xA5A5'5A5AULL};
      std::shuffle(entries.begin(), entries.end(), rng);
      break;
    }
    case WorkloadKind::kDeepSpine32Shuffled: {
      // 231 symbols at length 8, plus 25 symbols descending to max depth 32
      for (std::size_t i = 0U; i < 231U; ++i) {
        entries[i].length = 8U;
      }
      for (std::size_t i = 0U; i < 23U; ++i) {
        entries[231U + i].length = static_cast<std::uint8_t>(9U + i);
      }
      entries[254U].length = 32U;
      entries[255U].length = 32U;
      std::mt19937_64 rng{kDeterministicBenchmarkSeed ^ 0xDEAD'BEEFULL};
      std::shuffle(entries.begin(), entries.end(), rng);
      break;
    }
    case WorkloadKind::kReverseSorted8Bit: {
      for (std::size_t i = 0U; i < HUFFMAN_MAX_SYMBOLS; ++i) {
        entries[i].symbol = static_cast<std::uint8_t>(255U - i);
        entries[i].length = 8U;
      }
      break;
    }
  }

  return entries;
}

void BM_HuffmanTree_C_Baseline(benchmark::State& state,
                               const WorkloadKind kind) {
  const auto entries = make_256_entries(kind);
  struct huffman_tree tree{};

  static_cast<void>(huffman_tree_build(&tree, entries.data(), entries.size()));

  for (auto _ : state) {
    enum huffman_error err =
        huffman_tree_build(&tree, entries.data(), entries.size());
    benchmark::DoNotOptimize(err);
    benchmark::DoNotOptimize(tree.root);
    benchmark::DoNotOptimize(tree.node_count);
    benchmark::ClobberMemory();
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(entries.size()));
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(sizeof(entries)));
}

void BM_HuffmanTree_C_Candidate(benchmark::State& state,
                                const WorkloadKind kind) {
  const auto entries = make_256_entries(kind);
  struct huffman_tree tree{};

  static_cast<void>(
      huffman_tree_build_optimized(&tree, entries.data(), entries.size()));

  for (auto _ : state) {
    enum huffman_error err =
        huffman_tree_build_optimized(&tree, entries.data(), entries.size());
    benchmark::DoNotOptimize(err);
    benchmark::DoNotOptimize(tree.root);
    benchmark::DoNotOptimize(tree.node_count);
    benchmark::ClobberMemory();
  }

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(entries.size()));
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(sizeof(entries)));
}

#define REGISTER_HUFFMAN_C_BENCHMARK(label, kind)            \
  BENCHMARK_CAPTURE(BM_HuffmanTree_C_Baseline, label, kind)  \
      ->Name("BM_HuffmanTree_C_Baseline/" #label);           \
  BENCHMARK_CAPTURE(BM_HuffmanTree_C_Candidate, label, kind) \
      ->Name("BM_HuffmanTree_C_Candidate/" #label)

REGISTER_HUFFMAN_C_BENCHMARK(256_Uniform8Bit_Shuffled,
                             WorkloadKind::kUniform8BitShuffled);
REGISTER_HUFFMAN_C_BENCHMARK(256_DeflateSkewed_Shuffled,
                             WorkloadKind::kDeflateSkewedShuffled);
REGISTER_HUFFMAN_C_BENCHMARK(256_DeepSpine32_Shuffled,
                             WorkloadKind::kDeepSpine32Shuffled);
REGISTER_HUFFMAN_C_BENCHMARK(256_ReverseSorted8Bit,
                             WorkloadKind::kReverseSorted8Bit);

#undef REGISTER_HUFFMAN_C_BENCHMARK

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
  static_cast<void>(
      pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0));

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
