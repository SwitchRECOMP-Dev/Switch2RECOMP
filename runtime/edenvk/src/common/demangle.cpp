// SwitchRecomp: names are shown as they are (Eden demangles through LLVM's demangler, not built here)
#include "common/demangle.h"

namespace Common {

std::string DemangleSymbol(const std::string& mangled) {
    return mangled;
}

} // namespace Common
