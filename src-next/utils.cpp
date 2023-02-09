/*
 * ShortCircuit XT - a Surge Synth Team product
 *
 * A fully featured creative sampler, available as a standalone
 * and plugin for multiple platforms.
 *
 * Copyright 2019 - 2023, Various authors, as described in the github
 * transaction log.
 *
 * ShortCircuitXT is released under the Gnu General Public Licence
 * V3 or later (GPL-3.0-or-later). The license is found in the file
 * "LICENSE" in the root of this repository or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 *
 * Individual sections of code which comprises ShortCircuitXT in this
 * repository may also be used under an MIT license. Please see the
 * section  "Licensing" in "README.md" for details.
 *
 * ShortCircuitXT is inspired by, and shares some code with, the
 * commercial product ShortCircuit 1 and 2, released by VemberTech
 * in the mid 2000s. The code for ShortCircuit 2 was opensourced in
 * 2020 at the outset of this project.
 *
 * All source for ShortCircuitXT is available at
 * https://github.com/surge-synthesizer/shortcircuit-xt
 */

#include <iostream>
#include "utils.h"

namespace scxt
{
#if USE_SIMPLE_LEAK_DETECTOR
std::map<std::string, std::pair<int, int>> allocLog;
void showLeakLog()
{
    std::cout << "\nMemory Allocation Log of NoCopy classes\n";
    for (const auto &[k, v] : allocLog)
    {
        auto [a, d] = v;
        std::cout << "   class=" << k << "  ctor=" << a << " dtor=" << d
                  << ((a != d) ? " ERROR!!" : " OK") << std::endl;
    }
}
#endif

} // namespace scxt