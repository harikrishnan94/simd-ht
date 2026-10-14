project(
  'simd-ht',
  'cpp',
  version: '0.1.0',
  meson_version: '>=1.3.0',
  default_options: ['cpp_std=c++20', 'buildtype=release', 'warning_level=3', 'b_ndebug=if-release'],
)

cpp = meson.get_compiler('cpp')
if cpp.get_id() != 'clang'
  error('Use Clang: CXX=clang++ meson setup build')
endif

simd = get_option('simd')
cpu = host_machine.cpu_family()
if simd == 'auto'
  if cpu == 'aarch64'
    simd = 'neon64'
  elif cpu == 'x86_64'
    simd = 'avx2'
  else
    error('Supported targets are AArch64 NEON and x86-64 AVX2')
  endif
endif

# Explicit vector-type operations remain vectorized; scalar controls must stay scalar.
no_autovec_args = ['-fno-vectorize', '-fno-slp-vectorize']
if simd == 'neon64' and cpu == 'aarch64'
  table_args = no_autovec_args + ['-DSIMD_HT_NEON64=1']
  other_simd = 'avx2'
elif simd == 'avx2' and cpu == 'x86_64'
  table_args = no_autovec_args + ['-DSIMD_HT_AVX2=1', '-mavx2']
  other_simd = 'neon64'
else
  error('The selected SIMD backend does not match the target CPU family')
endif

cmake = import('cmake')
benchmark_options = cmake.subproject_options()
benchmark_options.add_cmake_defines({
  'BENCHMARK_ENABLE_TESTING': false,
  'BENCHMARK_ENABLE_GTEST_TESTS': false,
  'BENCHMARK_ENABLE_INSTALL': false,
  'BENCHMARK_ENABLE_WERROR': false,
  'BENCHMARK_ENABLE_LIBPFM': false,
  'CMAKE_BUILD_TYPE': 'Release',
})
benchmark_options.set_install(false)
benchmark_project = cmake.subproject('benchmark', options: benchmark_options)
benchmark_dep = benchmark_project.dependency('benchmark', include_type: 'system')

table_dep = declare_dependency(
  include_directories: include_directories('src'),
  compile_args: table_args,
)

benchmark_source = files('src/benchmark.cpp')
test_source = files('tests/hash_tables_test.cpp')
probe_stats_source = files('tools/probe_stats.cpp')
project_sources = benchmark_source + test_source + probe_stats_source + files('src/amac.hpp', 'src/hash_tables.hpp', 'src/perf_counters.hpp', 'src/workload.hpp')

oa_bench = executable('oa_bench', benchmark_source, dependencies: [table_dep, benchmark_dep])
oa_tests = executable('oa_tests', test_source, dependencies: table_dep)
oa_probe_stats = executable('oa_probe_stats', probe_stats_source, dependencies: table_dep)
test('hash_tables', oa_tests, timeout: 30)
# The lookups use only Clang vector types, so the other backend's source also builds for this CPU.
other_table_dep = declare_dependency(
  include_directories: include_directories('src'),
  compile_args: no_autovec_args + ['-DSIMD_HT_@0@=1'.format(other_simd.to_upper())],
)
oa_tests_other = executable('oa_tests_' + other_simd, test_source, dependencies: other_table_dep)
test('hash_tables_' + other_simd + '_source', oa_tests_other, timeout: 30)
test('probe_stats_placement', oa_probe_stats, args: ['--verify', '16'], timeout: 120)
test('benchmark_smoke', oa_bench,
  args: ['--benchmark_filter=.*keys:1024/', '--benchmark_min_time=0.001s', '--benchmark_repetitions=1'],
  timeout: 120,
)

clang_format = find_program('clang-format-22', 'clang-format-21', 'clang-format-20', 'clang-format-19', 'clang-format')
clang_tidy = find_program('clang-tidy-22', 'clang-tidy-21', 'clang-tidy-20', 'clang-tidy-19', 'clang-tidy')
run_target('format', command: [clang_format, '-i'] + project_sources)
run_target('format-check', command: [clang_format, '--dry-run', '--Werror'] + project_sources)
run_target('tidy', command: [clang_tidy, '-p', meson.project_build_root(), '--quiet'] + benchmark_source + test_source + probe_stats_source)

summary({
  'SIMD backend': simd,
  'Google Benchmark': '1.9.4',
  'Automatic vectorization': 'disabled for benchmark and tests',
}, section: 'Benchmark configuration')
