# symbolic-test-generator

[![ci](https://github.com/adwitiyashukla/symbolic-test-generator/actions/workflows/ci.yml/badge.svg)](https://github.com/adwitiyashukla/symbolic-test-generator/actions/workflows/ci.yml)

Point it at a C function and it works out the inputs that reach every branch, instead of guessing
them the way a fuzzer does. It runs the function on symbolic values, collects the constraints along
each path, and solves them. What comes out is a set of concrete test inputs and a C harness you can
compile and run.

Everything here is written from scratch in C++20 with no third party libraries: the C front end, the
intermediate representation, the symbolic executor, the bitvector layer, the bit blaster and the CDCL
SAT solver.

## Build

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

## Try it

```
build/symx explore targets/license_key.c
```

`targets/license_key.c` accepts an 8 byte key only if the first byte is 88 and a rolling checksum of
all 8 bytes lands on one exact 32 bit value. The engine finds a key in two milliseconds:

```
branch coverage   6 of 6 directions (100 percent)
tests written     3
errors found      1
solver seconds    0.0016823

errors
  line 15 column 9: assertion failure, assertion can fail
    input key = {88, 228, 21, 7, 29, 10, 6, 0}
```

Random search has to satisfy an 8 bit prefix and a 32 bit checksum at the same time, so it would need
somewhere near 2^40 attempts. Two thousand random inputs never got there.

The other command compiles the generated tests with the system C compiler and runs them:

```
build/symx verify targets/date.c --count 2000
```

## Results

Measured on my laptop, an HP OmniBook 5 Flip 14 with an Intel Core 5 120U and 8 GB of RAM, on
Windows 11 with MinGW gcc 15.2. Branch percentages in the last two columns come from gcov, not from
the engine, so the comparison is done by a different tool.

| target | input bytes | tests | seconds | branch directions | gcov branches, generated tests | gcov branches, 2000 random inputs |
| --- | --- | --- | --- | --- | --- | --- |
| triangle | 12 | 11 | 0.068 | 22 of 22 | 100.00 percent of 22 | 72.73 percent |
| date | 12 | 25 | 0.299 | 34 of 34 | 94.74 percent of 38 | 7.89 percent |
| binary_search | 36 | 24 | 0.227 | 12 of 12 | 92.86 percent of 14 | 21.43 percent |
| parse_number | 8 | 32 | 0.242 | 11 of 12 | 91.67 percent of 12 | 83.33 percent |
| license_key | 8 | 3 | 0.002 | 6 of 6 | 83.33 percent of 6 | 83.33 percent |
| faults | 40 | 8 | 0.260 | 8 of 8 | 90.00 percent of 10 | 80.00 percent |

`date.c` is the widest gap. It only does interesting work for years between 1900 and 2100 and months
between 1 and 12, so random 32 bit integers land in the early return almost every time. Twenty five
generated tests reach 94.74 percent of the branches gcov counts, against 7.89 percent for two
thousand random ones.

The one direction the engine misses in `parse_number.c` is a check for a value above 2147483647. The
input is 8 bytes, so the largest number the loop can build is 9999999, and that branch cannot be
taken. The engine reports it as uncovered rather than pretending otherwise.

### Errors

`targets/faults.c` has four separate problems planted in it, and the engine finds all four with an
input for each:

| line | error | witness |
| --- | --- | --- |
| 10 | out of bounds write | `index = 2005958645` |
| 17 | division by zero | `divisor = 0` |
| 17 | signed division overflow | `divisor = -1` with `values` summing to the smallest int |
| 19 | assertion failure | four large negative values whose sum wraps to exactly 42 |

Every one of those inputs was compiled with gcc and run, and every one failed the way the engine said
it would.

### Solver optimizations

Two things keep the solver work down. Queries are cached by the set of constraints they contain, and
constraints that share no symbolic bytes with the question being asked are dropped before the query
is built. Turning both off:

| target | solver seconds, off | solver seconds, on | CNF gates, off | CNF gates, on |
| --- | --- | --- | --- | --- |
| date | 0.364 | 0.150 | 496,637 | 228,181 |
| parse_number | 0.455 | 0.131 | 363,298 | 203,395 |
| binary_search | 0.191 | 0.174 | 86,121 | 86,121 |

`binary_search.c` gets nothing from either, because all 36 input bytes end up tangled in one
constraint set and no query repeats.

## How it works

### Front end

`src/ir` holds a lexer, a recursive descent parser and a lowering pass. The lowering pass does the
type checking as it goes and produces a register based intermediate representation with basic blocks,
in the same shape a C compiler produces at -O0: locals live in memory, and reads and writes go
through explicit load and store instructions. Allocations are hoisted to the entry block so a loop
body does not allocate on every iteration.

### Symbolic execution

`src/exec` walks that representation. Every register holds an expression instead of a number. At a
branch the engine asks the solver whether each side is reachable, and if both are it clones the state
and follows both. Four search orders are available: depth first, breadth first,
random, and one that favors the state which found new code most recently. All four reach the same
coverage on the targets here, because these path counts are small enough to explore in full. The
order starts to matter once the search has to stop early.

Assertions, division and memory accesses are checked as they execute. When a check can fail, the
engine records the failing input, adds the constraint that says the failure did not happen, and keeps
going down the safe path.

### Memory

Every object gets an id, and its address is that id shifted left by 32 bits. So the object an address
belongs to is the top half and the offset inside it is the bottom half, which makes pointer
arithmetic ordinary bitvector arithmetic. Reads and writes at a symbolic offset build a chain of
conditional selects over the positions the offset could take, and past a configurable object size the
offset is pinned to one concrete value instead.

The first version of the bounds check compared the low 32 bits of the address against the object
size. That looked right until gcc segfaulted on an input the engine had cleared: with `index` set to
1073741824, `index * 4` is exactly 2^32, the low half came out as zero and the access looked
in bounds while the real address had walked into the next object. The check now subtracts the object
base from the full 64 bit address and compares that difference, so the carry cannot hide.

### Solver

`src/solver` has four layers. Expressions are hash consed and simplified as they are built, so
constant folding, identities and strength reduction happen before anything reaches the solver.
Multiplication by a constant with three or fewer set bits becomes shifts and adds, which is what
keeps `parse_number.c` cheap, since it multiplies by 10 on every digit.

Above that is a query layer with the cache and the independence pruning from the table earlier. Below
it, the bit blaster turns bitvector constraints into CNF with a Tseitin encoding, including ripple
carry adders, a shift and add multiplier, barrel shifters and a restoring divider. Gates whose inputs
are already constant are folded rather than emitted.

At the bottom is a CDCL SAT solver with two watched literals, VSIDS branching with decay, first UIP
clause learning, learned clause minimization, non chronological backjumping, phase saving, Luby
restarts and clause database reduction by LBD. It reads DIMACS too:

```
build/symx sat problem.cnf
```

## The C it accepts

Integer types and pointers to them, arrays including multidimensional ones, globals, all the
arithmetic, bitwise, comparison and assignment operators, `if`, `while`, `do`, `for`, `break`,
`continue`, `return`, `sizeof`, casts, the conditional operator, and function calls including
recursion. There is no preprocessor beyond `#define` of an integer constant, and `#include` lines are
skipped, so a target can include `assert.h` and still be read by both the engine and a real compiler.
No floats, no structs, no unions, no function pointers.

Parameters of the entry function are the symbolic input. A scalar parameter becomes that many
symbolic bytes, and a pointer parameter becomes a buffer of `input.buffer_elements` elements.
`assert(x)` is checked, and `symx_assume(x)` narrows the input space without being treated as a bug.

## How the numbers were checked

`symx verify` does not trust the engine's own report. It writes the generated inputs into a C
harness, compiles the harness and the target with the system compiler under `-fprofile-arcs
-ftest-coverage`, runs each test in its own process, and reads the coverage back out with gcov. It
then does the same for the random inputs, so both columns of the table come from the same tool.

It also checks two more things. Every input the engine flagged as an error has to actually fail when
the compiled program runs it, and none of the other generated inputs may fail. On top of that, the
engine's own interpreter and the compiled program are run on the same 2000 random inputs and their
return values compared. Across the six targets that is 11,022 comparisons with no
disagreement. The rest are inputs where the C program has no defined answer, which the tool skips,
and inputs that crash the program outright, which nearly half of them do on `faults.c`.

That last check is what caught the address bug described earlier.

## Tests

Seven test binaries, run by ctest. The SAT solver is checked against a brute force enumeration on 400
random formulas. The bitvector solver is checked the same way, over every assignment of two symbolic
bytes, so a wrong answer from the bit blaster or the simplifier shows up as a disagreement. The front
end runs snippets and compares results against what C says they should be. The executor tests check
that generated tests replay to the same coverage and that each planted error is found.

```
ctest --test-dir build --output-on-failure
```

CI builds on Linux with gcc and clang, on macOS, and on Windows with MSVC, runs the suite under
address and undefined behavior sanitizers, checks formatting, and runs `symx verify` over every
target.

## Configuration

`config.toml` holds the search strategy, the limits on states, steps, forks, call depth and time, the
buffer size for pointer parameters, the shift check, and the solver switches. Every key is validated,
and an unknown key or an out of range value is an error rather than something silently ignored. Any
of it can be overridden on the command line.

## License

MIT
