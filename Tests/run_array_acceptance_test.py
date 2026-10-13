import os
from pathlib import Path
import subprocess
import re

root = Path(__file__).resolve().parents[1]
source = (root / 'Source/src/Main.cpp').read_text()
start = source.index('bool complete=true;')
end = source.index('if(complete) ArrayWrites::cached', start)
condition = source[start:end]
code = r'''
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>
using UINT=unsigned;
struct Sample { float r,g,b; };
bool accepts(const std::vector<Sample>& samples) {
    const auto* values=samples.data();
    const UINT g_tintProbePendingCubes=static_cast<UINT>(samples.size());
''' + condition + r'''
    return complete;
}
int main() {
    // Reproduce a partly populated 42-cube material array.
    std::vector<Sample> samples(42,{0,0,0});
    samples[0]={0.1f,0.2f,0.3f};
    assert(accepts(samples));
    samples[17]={0.000001f,0.000001f,0.000001f};
    assert(accepts(samples));
    samples[17]={0.5f,0.3f,0.1f};
    assert(accepts(samples));
    samples[41].r=std::numeric_limits<float>::quiet_NaN();
    assert(!accepts(samples));
    samples[41].r=std::numeric_limits<float>::infinity();
    assert(!accepts(samples));
    samples[41].r=-std::numeric_limits<float>::infinity();
    assert(!accepts(samples));
}
'''
out = root / 'Tests/temp'
out.mkdir(exist_ok=True)
(out/'array_acceptance.cpp').write_text(code)
subprocess.run([os.environ.get('CXX','clang++'),
                '-std=c++20',str(out/'array_acceptance.cpp'),'-o',str(out/'array_acceptance.exe')],check=True)
subprocess.run([str(out/'array_acceptance.exe')],check=True)

print("PASS: production source/cache acceptance checks")
