
#include "LinkTimeRelocationsStep.hpp"
#include <cstdint>
#include <limits>
#include <dobby.h>
#ifdef __aarch64__
#include <mach-o/arm64/reloc.h>
#endif
#include "jet/live/DataTypes.hpp"
#include "jet/live/LiveContext.hpp"
#include "jet/live/Utility.hpp"

namespace jet
{
    void LinkTimeRelocationsStep::reload(LiveContext* context, Program* newProgram)
    {
        context->events->addLog(LogSeverity::kDebug, "Loading link-time relocations...");

        const auto& relocs = context->programInfoLoader->getLinkTimeRelocations(context, newProgram->objFilePaths);
        auto totalRelocs = relocs.size();
        size_t appliedRelocs = 0;
        std::vector<Symbol> relocatedSymbols;
        for (const auto& reloc : relocs) {
            const Symbol* targetSymbol =
                findFunction(newProgram->symbols, reloc.targetSymbolName, reloc.targetSymbolHash);
            if (!targetSymbol) {
                context->events->addLog(LogSeverity::kError,
                    "targetSymbol not found: " + reloc.targetSymbolName + " " + std::to_string(reloc.targetSymbolHash));
                continue;
            }

            const Symbol* relocSymbol =
                findVariable(newProgram->symbols, reloc.relocationSymbolName, reloc.relocationSymbolHash);
            if (!relocSymbol) {
                context->events->addLog(LogSeverity::kError,
                    "relocSymbol not found: " + reloc.relocationSymbolName + " "
                        + std::to_string(reloc.relocationSymbolHash));
                continue;
            }

            const Symbol* oldVar = nullptr;
            auto& progs = context->programs;
            for (const auto& prog : progs) {
                oldVar = findVariable(prog.symbols, reloc.relocationSymbolName, reloc.relocationSymbolHash);
                if (oldVar) {
                    break;
                }
            }
            if (!oldVar) {
                continue;
            }

            auto relocAddress = targetSymbol->runtimeAddress + reloc.relocationOffsetRelativeTargetSymbolAddress;
            auto distance = std::abs(static_cast<intptr_t>(oldVar->runtimeAddress - relocSymbol->runtimeAddress));
            int64_t maxAllowedDistance = 0;
            if (reloc.size == sizeof(int32_t)) {
                maxAllowedDistance = std::numeric_limits<int32_t>::max();
            } else if (reloc.size == sizeof(int64_t)) {
                maxAllowedDistance = std::numeric_limits<int64_t>::max();
            } else {
                context->events->addLog(LogSeverity::kError, "LinkTimeRelocationsStep: WTF");
                continue;
            }
            if (distance > maxAllowedDistance) {
                context->events->addLog(LogSeverity::kWarning,
                    "Cannot apply relocation for " + relocSymbol->name
                        + ", distance doesn't fit into max allowed distance");
                continue;
            }

            auto relocAddressPtr = reinterpret_cast<void*>(relocAddress);
#ifdef __aarch64__
            if (reloc.type == ARM64_RELOC_UNSIGNED) {
                // Проверяем размер релокации
                if (reloc.size == 4) {
                    uint32_t buf = oldVar->runtimeAddress;
                    if (DobbyCodePatch(relocAddressPtr, reinterpret_cast<uint8_t*>(&buf), sizeof(buf)) != 0) {
                        context->events->addLog(LogSeverity::kError, "relocation code patch failed");
                        continue;
                    }
                } else if (reloc.size == 8) {
                    uint64_t buf = oldVar->runtimeAddress;
                    if (DobbyCodePatch(relocAddressPtr, reinterpret_cast<uint8_t*>(&buf), sizeof(buf)) != 0) {
                        context->events->addLog(LogSeverity::kError, "relocation code patch failed");
                        continue;
                    }
                }
            } else if (reloc.type == ARM64_RELOC_PAGE21) {
                uint32_t instr = *((uint32_t*)relocAddressPtr);
                uint64_t A = oldVar->runtimeAddress;
                uint64_t P = relocAddress;
                int64_t pageDiff = (A & ~0xFFF) - (P & ~0xFFF);
                int32_t new_imm = pageDiff >> 12;
                // Проверка диапазона new_imm
                uint32_t immlo = (new_imm & 0x3) << 29;
                uint32_t immhi = (new_imm & 0x1FFFFC) << 3;
                instr = (instr & ~0x60000000) | immlo;
                instr = (instr & ~0x00FFFFE0) | immhi;
                if (DobbyCodePatch(relocAddressPtr, (uint8_t*)&instr, sizeof(instr)) != 0) {
                    context->events->addLog(LogSeverity::kError, "relocation code patch failed");
                    continue;
                }
            } else if (reloc.type == ARM64_RELOC_PAGEOFF12) {
                uint32_t instr = *((uint32_t*)relocAddressPtr);
                uint64_t A = oldVar->runtimeAddress;
                uint32_t offset = A & 0xFFF;
    
                // Определяем тип инструкции по её опкоду
                uint32_t opcode = instr & 0xFF000000;
                uint32_t opcode2 = instr & 0xFFC00000;
    
                if ((opcode2 & 0x3B000000) == 0x39000000) {
                    // Load/Store instructions (LDR, STR, etc.)
                    // Определяем размер данных по битам 31-30
                    uint32_t size = (instr >> 30) & 0x3;
                    uint32_t scale;
        
                    switch (size) {
                        case 0: // 8-bit (byte)
                            scale = 0;
                            break;
                        case 1: // 16-bit (half-word)
                            scale = 1;
                            break;
                        case 2: // 32-bit (word)
                            scale = 2;
                            break;
                        case 3: // 64-bit (double-word)
                            scale = 3;
                            break;
                        default:
                            scale = 0;
                    }
        
                    // Масштабируем смещение
                    offset = offset >> scale;
                    instr &= ~0x003FFC00;
                    instr |= (offset << 10);
                }
                else if ((opcode & 0x7F800000) == 0x11000000) {
                    // Add immediate instructions (ADD, ADDS)
                    // Format: <Xd>, <Xn>, #<imm>{, <shift>}
                    // Immediate field: bits [21:10]
                    uint32_t shift = (instr & 0x00C00000) >> 22; // Get shift field
                    offset = offset >> shift; // Scale the offset according to the shift
                    instr &= ~0x003FFC00;
                    instr |= (offset << 10);
                }
                else if ((opcode2 & 0x3F800000) == 0x38000000) {
                    // Load/Store register (unsigned immediate) instructions
                    // Format: [<Xn|SP>{, #<pimm>}]
                    // Immediate field: bits [21:10]
                    uint32_t size = (instr & 0xC0000000) >> 30;
                    uint32_t scale = size; // Scale factor is 2^size
                    offset = offset >> scale;
                    instr &= ~0x003FFC00;
                    instr |= (offset << 10);
                }
                else {
                    context->events->addLog(LogSeverity::kError,
                                            "Unsupported instruction type for PAGEOFF12 relocation: 0x" +
                                            std::to_string(instr));
                    continue;
                }

                if (DobbyCodePatch(relocAddressPtr, (uint8_t*)&instr, sizeof(instr)) != 0) {
                    context->events->addLog(LogSeverity::kError, "relocation code patch failed");
                    continue;
                }
            }
#else
            if (reloc.size == sizeof(int32_t)) {
                int32_t buf = *reinterpret_cast<int32_t*>(relocAddressPtr) +
                    static_cast<int32_t>(oldVar->runtimeAddress) - static_cast<int32_t>(relocSymbol->runtimeAddress);
                if (DobbyCodePatch(relocAddressPtr, reinterpret_cast<uint8_t*>(&buf), sizeof(buf)) != 0) {
                    context->events->addLog(LogSeverity::kError, "relocation code patch failed");
                    continue;
                }
            } else if (reloc.size == sizeof(int64_t)) {
                int64_t buf = *reinterpret_cast<int64_t*>(relocAddressPtr) +
                    static_cast<int32_t>(oldVar->runtimeAddress) - static_cast<int32_t>(relocSymbol->runtimeAddress);
                if (DobbyCodePatch(relocAddressPtr, reinterpret_cast<uint8_t*>(&buf), sizeof(buf)) != 0) {
                    context->events->addLog(LogSeverity::kError, "relocation code patch failed");
                    continue;
                }
            }
#endif
            context->events->addLog(LogSeverity::kDebug, relocSymbol->name + " was relocated");

            relocatedSymbols.push_back(*relocSymbol);
            appliedRelocs++;
        }

        for (const auto& relocSymbol : relocatedSymbols) {
            auto& newVars = newProgram->symbols.variables[relocSymbol.name];
            for (size_t i = 0; i < newVars.size(); i++) {
                if (newVars[i].hash == relocSymbol.hash) {
                    newVars.erase(newVars.begin() + i);
                    break;
                }
            }
            if (newVars.empty()) {
                newProgram->symbols.variables.erase(relocSymbol.name);
            }
        }

        context->events->addLog(LogSeverity::kDebug,
            "Done, relocated: " + std::to_string(appliedRelocs) + "/" + std::to_string(totalRelocs));
    }
}
