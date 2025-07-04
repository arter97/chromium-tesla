// Copyright 2023 The Dawn & Tint Authors
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "src/tint/cmd/fuzz/ir/fuzz.h"

#include <cstddef>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#include "src/tint/utils/containers/vector.h"
#include "src/tint/utils/ice/ice.h"
#include "src/tint/utils/macros/defer.h"
#include "src/tint/utils/macros/static_init.h"

#if TINT_BUILD_WGSL_READER
#include "src/tint/cmd/fuzz/wgsl/fuzz.h"
#include "src/tint/lang/core/ir/validator.h"
#include "src/tint/lang/wgsl/ast/module.h"
#include "src/tint/lang/wgsl/helpers/apply_substitute_overrides.h"
#include "src/tint/lang/wgsl/reader/reader.h"
#endif

namespace tint::fuzz::ir {

#if TINT_BUILD_IR_BINARY
/// @returns a reference to the static list of registered IRFuzzers.
/// @note this is not a global, as the static initializers that register fuzzers may be called
/// before this vector is constructed.
Vector<IRFuzzer, 32>& Fuzzers() {
    static Vector<IRFuzzer, 32> fuzzers;
    return fuzzers;
}

thread_local std::string_view currently_running;

[[noreturn]] void TintInternalCompilerErrorReporter(const tint::InternalCompilerError& err) {
    std::cerr << "ICE while running fuzzer: '" << currently_running << "'" << '\n';
    std::cerr << err.Error() << "\n";
    __builtin_trap();
}
#endif

void Register(const IRFuzzer& fuzzer) {
#if TINT_BUILD_WGSL_READER
    wgsl::Register({
        fuzzer.name,
        [fn = fuzzer.fn](const Program& program, const fuzz::wgsl::Context& context,
                         Slice<const std::byte> data) {
            if (program.AST().Enables().Any(tint::wgsl::reader::IsUnsupportedByIR)) {
                return;
            }
            auto transformed = tint::wgsl::ApplySubstituteOverrides(program);
            auto& src = transformed ? transformed.value() : program;
            if (!src.IsValid()) {
                return;
            }
            auto ir = tint::wgsl::reader::ProgramToLoweredIR(src);
            if (ir != Success) {
                return;
            }
            if (auto val = core::ir::Validate(ir.Get()); val != Success) {
                TINT_ICE() << val.Failure();
            }
            fn(ir.Get(), data);
        },
    });
#endif

    Fuzzers().Push(fuzzer);
}

void Run(const std::function<tint::core::ir::Module()>& acquire_module,
         const Options& options,
         Slice<const std::byte> data) {
    tint::SetInternalCompilerErrorReporter(&TintInternalCompilerErrorReporter);

    // Ensure that fuzzers are sorted. Without this, the fuzzers may be registered in any order,
    // leading to non-determinism, which we must avoid.
    TINT_STATIC_INIT(Fuzzers().Sort([](auto& a, auto& b) { return a.name < b.name; }));

    // Run each of the program fuzzer functions
    if (options.run_concurrently) {
        const size_t n = Fuzzers().Length();
        tint::Vector<std::thread, 32> threads;
        threads.Reserve(n);
        for (size_t i = 0; i < n; i++) {
            if (!options.filter.empty() &&
                Fuzzers()[i].name.find(options.filter) == std::string::npos) {
                continue;
            }
            threads.Push(std::thread([i, &acquire_module, &data, &options] {
                auto& fuzzer = Fuzzers()[i];
                currently_running = fuzzer.name;
                if (options.verbose) {
                    std::cout << " • [" << i << "] Running: " << currently_running << '\n';
                }
                auto mod = acquire_module();
                fuzzer.fn(mod, data);
            }));
        }
        for (auto& thread : threads) {
            thread.join();
        }
    } else {
        TINT_DEFER(currently_running = "");
        for (auto& fuzzer : Fuzzers()) {
            if (!options.filter.empty() && fuzzer.name.find(options.filter) == std::string::npos) {
                continue;
            }

            currently_running = fuzzer.name;
            if (options.verbose) {
                std::cout << " • Running: " << currently_running << '\n';
            }
            auto mod = acquire_module();
            fuzzer.fn(mod, data);
        }
    }
}

}  // namespace tint::fuzz::ir
