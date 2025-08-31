
#include "FunctionsHookingStep.hpp"
#include <cstdint>
#include <memory>
#include <dobby.h>
#include "jet/live/LiveContext.hpp"
#include "jet/live/Utility.hpp"

namespace jet
{
    void FunctionsHookingStep::reload(LiveContext* context, Program* newProgram)
    {
        context->events->addLog(LogSeverity::kDebug, "Hooking functions...");

        auto totalFunctions = getTotalFunctions(newProgram->symbols);
        size_t hookedFunctions = 0;
        for (const auto& syms : newProgram->symbols.functions) {
            for (const auto& sym : syms.second) {
                void* oldFuncPtr = nullptr;
                const auto& progs = context->programs;
                for (auto it = progs.rbegin(); it != progs.rend(); it++) {
                    if (auto foundSym = findFunction(it->symbols, sym.name, sym.hash)) {
                        oldFuncPtr = reinterpret_cast<void*>(foundSym->runtimeAddress);
                        break;
                    }
                }
                if (!oldFuncPtr) {
                    continue;
                }

                auto newFuncPtr = reinterpret_cast<void*>(sym.runtimeAddress);
                if (auto dobbyStatus = DobbyHook(oldFuncPtr, newFuncPtr, nullptr); dobbyStatus != 0) {
                    context->events->addLog(LogSeverity::kError,
                        "Cannot hook function: " + sym.name + ", status " + std::to_string(dobbyStatus));
                } else {
                    hookedFunctions++;
                }
            }
        }

        context->events->addLog(LogSeverity::kDebug,
            "Done, hooked: " + std::to_string(hookedFunctions) + "/" + std::to_string(totalFunctions));
    }
}
