using Xunit.Sdk;
using Xunit.v3;

// Container tests each own a WSL session; running them in parallel exhausts the runtime,
// so the module suite runs sequentially.
[assembly: Parallelization(Mode = ParallelMode.None)]
