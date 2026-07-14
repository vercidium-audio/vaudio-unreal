# Overview

The overall goal is:
- Less null propagation: lots of places have "if (!something) return;" with no reasoning why it could be null. Rather than letting null checks permeate the codebase, fix them early. If it genuinely can be null, leave a comment explaining why
- Logging cleanup: always use VALog() instead of directly calling VaRawLog or UE_LOG. VALog() automatically appends the name of the current object (emitter, world, etc), file name and function name

