// ANVIL offline runner entry point (Qt-free, FSR-free).
// Skeleton: argument passing and version banner. The decode -> window ->
// stage-chain -> capture loop is implemented by the substrate wave
// (docs/anvil/RECONCILIATION_20261005.md 3.5/3.7).
#include <cstdio>

int main(int argc, char** argv) {
  std::fprintf(stderr,
               "anvil_runner (skeleton): Temporal Forge / ANVIL successor offline runner.\n"
               "Usage: anvil_runner --config <pipeline.json>\n"
               "Implementation pending the substrate wave; exiting 2.\n");
  (void)argc;
  (void)argv;
  return 2;
}
