# Performance Principles

Software quality and robustness take precedence over performance here. But, within those constraints, I will prioritize speed, and _then_ space.

## Module/Function-Level Performance
- Every method/function will be benchmarked and compared against approaches that produce the same result. The fastest one wins, and if it's a tie, the most space-efficient wins.
- Use data structures that are the most performant for a given situation.

## Architecture-Level Performance
- Minimize the number of steps/jumps needed to complete any actions.
- Simplify and tighten whenever possible, while prioritizing encapsulation.
