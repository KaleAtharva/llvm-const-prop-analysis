# LLVM Constant Propagation Analysis

## Overview

This project implements a **constant propagation analysis as an LLVM Function Pass** using lattice-based data-flow analysis. It analyzes LLVM IR to track constant values across basic blocks and determines how variable values evolve throughout a function's control-flow graph (CFG).

The analysis uses a worklist algorithm to propagate information between basic blocks until a fixed point is reached.

## Features

- **Lattice-Based Analysis:** Represents variable values using three abstract states: `TOP`, `CONSTANT`, and `BOTTOM`.
- **Worklist Algorithm:** Iteratively processes basic blocks and propagates data-flow information to successor blocks.
- **Control-Flow Analysis:** Merges information from predecessor basic blocks using a meet operation.
- **Arithmetic Operations:** Handles integer addition, subtraction, multiplication, and signed division.
- **Comparison Operations:** Evaluates integer comparison instructions when their operands are known.
- **Memory Operations:** Tracks values associated with allocation, store, and load instructions.
- **Function Calls:** Handles call instructions, including special handling for `printf` and `scanf`.
- **Analysis Output:** Generates text files containing LLVM IR instructions and their corresponding abstract values.

## Technical Details

### 1. Abstract Value Lattice

The analysis represents the abstract value of each variable using the following states:

| State | Meaning |
|---|---|
| `TOP` | The value is not yet determined. |
| `CONSTANT` | The value is known and represented by an integer constant. |
| `BOTTOM` | The value is treated as overdefined or cannot be represented by a single constant. |

### 2. Data-Flow Analysis

The analysis maintains two environments for each basic block:

- **IN:** Abstract variable values entering the basic block.
- **OUT:** Abstract variable values after processing the instructions in the basic block.

The algorithm works as follows:

1. Initialize the worklist with the function's entry basic block.
2. Compute the incoming environment by merging information from predecessor blocks.
3. Process the instructions in the basic block and update the abstract values.
4. Compare the resulting environment with the previously computed output.
5. If the output changes, add successor blocks to the worklist.
6. Continue until the worklist becomes empty.
7. Generate a report containing the instructions and their abstract values.

### 3. Intraprocedural Analysis

The pass performs **intraprocedural analysis**, operating on one function's control-flow graph at a time. It propagates information across basic blocks within that function but does not analyze callee function bodies or perform interprocedural constant propagation.

## Technologies Used

- C++
- LLVM
- LLVM IR
- Data-Flow Analysis
- Worklist Algorithm

## Output

The pass writes its analysis results to a text file under the `output/` directory.

The output file is named using the source module's filename, with a `.txt` extension. It contains the LLVM IR instructions alongside the abstract values computed by the analysis.

For example:

```text
--------------------
%1 = alloca i32 --> %1 = TOP
store i32 10, ptr %1 --> %1 = 10
