"""Exercise extracted production dispatch and draw routines using CPU mocks."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "Source/src/Main.cpp").read_text()
from source_tools import without_diagnostic_instrumentation
source = without_diagnostic_instrumentation(source)

def function(signature):
    start = source.index(signature)
    begin = source.index("{", start)
    level, end = 1, begin + 1
    while level:
        level += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"

with tempfile.TemporaryDirectory(prefix="dcf4-perf-") as directory:
    temp = Path(directory)
    (temp / "production_performance.inc").write_text(
        function("struct UniversalReplicationScope") + ";\n" +
        function("UINT PreparePoolUpdates(") + function("void UniversalDispatchPool(") +
        "template<class DrawCall>\n" + function("void DrawWithRainNeutralB12("))
    executable = temp / ("performance_test.exe" if os.name == "nt" else "performance_test")
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-Wall", "-Wextra",
                    "-I" + str(temp), str(root / "Tests/performance_test.cpp"),
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
