# SSA specialization and deoptimization guards

TurboJS uses runtime feedback to specialize optimizing SSA graphs without changing the compact baseline IR. Stable `int32` argument observations refine SSA argument types and create explicit `guard.int32` nodes.

Each guard owns a stable deoptimization exit ID. A future optimizing backend can lower that node to a tag/range check and reconstruct the baseline or interpreter state through the existing deoptimization metadata pipeline.

Dominance frontiers are recorded as block masks. They identify merge blocks where definitions require phi placement. The current builder handles proven two-way merges; general loop-carried renaming remains conservative until the full variable-stack renamer is complete.
